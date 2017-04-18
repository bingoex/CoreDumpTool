#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifndef __USE_GNU
#define __USE_GNU
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <execinfo.h> //backtrace
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/shm.h>
#include <limits.h>
#include <pthread.h>

#include "segv_api.h"


//写日志（含时间、文件名、行号）
#define LOG_WITH_TIME(fmt, args...) do {\
	if(g_pstLog){\
		LogWithTime(g_pstLog, 2, "%s:%d(%s): SEGV: " fmt, __FILE__, __LINE__, __FUNCTION__ , ## args);\
	}\
} while(0);

//直接写文件
#define DIRCT_LOG(fmt, args...) do {\
	if(g_pstLog){\
		Log(g_pstLog, fmt, ## args);\
	}\
} while(0);

static uint32_t g_ulStub = COREDUMP_VAL_STUB; //桩:全局变量
static volatile uint32_t * g_uStub_first = NULL; //指向自动变量桩
static volatile uint32_t * g_uStub_last = NULL; //指向自动变量桩

static volatile unsigned long g_Main_Stack_SP;// 保存main函数栈顶，32位机器为ESP(32bit)，64位为RSP(64bit)
sigjmp_buf g__bEnv;

pthread_mutex_t g_SegvMute;

static int disable_restart_on_core = 0;

static char g_sLinkPath[500]; //进程名字

FCheckSwitchFlag g_pfCheckSwichFlag = NULL;

static LogFile g_stLog;
static LogFile *g_pstLog = NULL;

static char *DateTimeStr(const int32_t *mytime)
{
	static char s[50];
	struct tm curr;
	time_t tC2Now = *mytime;
	curr = *localtime(&tC2Now);

	if (curr.tm_year > 50) {
		sprintf(s, "%04d-%02d-%02d %02d:%02d:%02d",
					curr.tm_year+1900, curr.tm_mon+1, curr.tm_mday,
					curr.tm_hour, curr.tm_min, curr.tm_sec);
	} else {
		sprintf(s, "%04d-%02d-%02d %02d:%02d:%02d",
					curr.tm_year+2000, curr.tm_mon+1, curr.tm_mday,
					curr.tm_hour, curr.tm_min, curr.tm_sec);
	}

	return s;
}

static char *CurrDateTimeStr(void)
{
	int32_t iCurTime;
	iCurTime = time(NULL);
	return DateTimeStr(&iCurTime);
}

static char *DateTimeStrRaw(const time_t tTime)
{
	static char s[50];
	struct tm curr;
	curr = *localtime(&tTime);

	if (curr.tm_year > 50) {
		sprintf(s, "%04d-%02d-%02d %02d:%02d:%02d",
					curr.tm_year+1900, curr.tm_mon+1, curr.tm_mday,
					curr.tm_hour, curr.tm_min, curr.tm_sec);
	} else {
		sprintf(s, "%04d-%02d-%02d %02d:%02d:%02d",
					curr.tm_year+2000, curr.tm_mon+1, curr.tm_mday,
					curr.tm_hour, curr.tm_min, curr.tm_sec);
	}

	return s;
}

// 跟comm库日志函数类似
static int ShiftFiles(LogFile* pstLogFile)
{
	struct stat stStat;
	char sLogFileName[512];
	char sNewLogFileName[512];
	int i;
	struct tm stLogTm, stShiftTm;
	time_t tC2Now;

	sprintf(sLogFileName,"%s.log", pstLogFile->sBaseFileName);

	if(stat(sLogFileName, &stStat) < 0) return -1;
	switch (pstLogFile->iShiftType) {
		case 0:
			if (stStat.st_size < pstLogFile->lMaxSize) return 0;
			break;
		case 2:
			if (stStat.st_mtime - pstLogFile->lLastShiftTime < pstLogFile->lMaxCount) return 0;
			break;
		case 3:
			if (pstLogFile->lLastShiftTime - stStat.st_mtime > 86400) break;
			memcpy(&stLogTm, localtime(&stStat.st_mtime), sizeof(stLogTm));
			tC2Now = pstLogFile->lLastShiftTime;
			memcpy(&stShiftTm, localtime(&tC2Now), sizeof(stShiftTm));
			if (stLogTm.tm_mday == stShiftTm.tm_mday) return 0;
			break;
		case 4:
			if (pstLogFile->lLastShiftTime - stStat.st_mtime > 3600) break;
			memcpy(&stLogTm, localtime(&stStat.st_mtime), sizeof(stLogTm));
			tC2Now = pstLogFile->lLastShiftTime;
			memcpy(&stShiftTm, localtime(&tC2Now), sizeof(stShiftTm));
			if (stLogTm.tm_hour == stShiftTm.tm_hour) return 0;
			break;
		case 5:
			if (pstLogFile->lLastShiftTime - stStat.st_mtime > 60) break;
			memcpy(&stLogTm, localtime(&stStat.st_mtime), sizeof(stLogTm));
			tC2Now = pstLogFile->lLastShiftTime;
			memcpy(&stShiftTm, localtime(&tC2Now), sizeof(stShiftTm));
			if (stLogTm.tm_min == stShiftTm.tm_min) return 0;
			break;
		default:
			if (pstLogFile->lLogCount < pstLogFile->lMaxCount) return 0;
			pstLogFile->lLogCount = 0;
	}

	//走到这里说明需要轮转log文件了
	for(i = pstLogFile->iMaxLogNum-2; i >= 0; i--) {
		if (i == 0)
			sprintf(sLogFileName,"%s.log", pstLogFile->sBaseFileName);
		else
			sprintf(sLogFileName,"%s%d.log", pstLogFile->sBaseFileName, i);

		if (access(sLogFileName, F_OK) == 0) {
			sprintf(sNewLogFileName,"%s%d.log", pstLogFile->sBaseFileName, i+1);
			if (rename(sLogFileName,sNewLogFileName) < 0 ) {
				return -1;
			}
		}
	}

	pstLogFile->lLastShiftTime = time(NULL);

	return 0;
}

static int32_t InitLogFile(LogFile* pstLogFile, const char* sLogBaseName, int32_t iShiftType, int32_t iMaxLogNum, int32_t iMAX)
{
	memset(pstLogFile, 0, sizeof(LogFile));
	strncat(pstLogFile->sLogFileName, sLogBaseName, sizeof(pstLogFile->sLogFileName) - 10);
	strcat(pstLogFile->sLogFileName, ".log");

	strncpy(pstLogFile->sBaseFileName, sLogBaseName, sizeof(pstLogFile->sBaseFileName) - 1);
	pstLogFile->iShiftType = iShiftType;
	pstLogFile->iMaxLogNum = iMaxLogNum;
	pstLogFile->lMaxSize = iMAX;
	pstLogFile->lMaxCount = iMAX;
	pstLogFile->lLogCount = iMAX;
	pstLogFile->lLastShiftTime = time(NULL);

	return ShiftFiles(pstLogFile);
}


int LogWithTime(LogFile* pstLogFile, int iLogTime,const char* sFormat, ...)
{
	va_list ap;
	struct timeval stLogTv;
	if (NULL == pstLogFile) {
		return -1;
	}

	if ((pstLogFile->pLogFile = fopen(pstLogFile->sLogFileName, "a+")) == NULL) return -1;
	va_start(ap, sFormat);
	if (iLogTime == 1) {
		fprintf(pstLogFile->pLogFile, "[%s] ", CurrDateTimeStr());
	}
	else if (iLogTime == 2) {
		gettimeofday(&stLogTv, NULL);
		fprintf(pstLogFile->pLogFile, "[%s.%.6u] ", DateTimeStrRaw(stLogTv.tv_sec), (unsigned int)stLogTv.tv_usec);
	}

	vfprintf(pstLogFile->pLogFile, sFormat, ap);
	fprintf(pstLogFile->pLogFile, "\n");
	va_end(ap);

	pstLogFile->lLogCount++;
	fclose(pstLogFile->pLogFile);

	return ShiftFiles(pstLogFile);
}

static int CheckSwichFlag()
{
	if(g_pfCheckSwichFlag)
		return (*g_pfCheckSwichFlag)();
	else
		return 0;
}

static int CheckStub()
{
	if(NULL == g_uStub_first || *g_uStub_first != COREDUMP_VAL_STUB){
		LOG_WITH_TIME("stub_first check failed.");
		return 0;
	}

	if(NULL == g_uStub_last || *g_uStub_last != COREDUMP_VAL_STUB){
		LOG_WITH_TIME("stub_last check failed.");
		return 0;
	}

	if(g_ulStub != COREDUMP_VAL_STUB){
		LOG_WITH_TIME("static globle stub check failed.");
		return 0;
	}

	return 1;
}

// 频率限制
static int FreqLimit()
{
	//siglongjmp的效率大约为5000/s
	return 1;
}


static int Log(LogFile* pstLogFile, const char* sFormat, ...)
{
	va_list ap;
	if ((pstLogFile->pLogFile = fopen(pstLogFile->sLogFileName, "a+")) == NULL) return -1;
	va_start(ap, sFormat);

	vfprintf(pstLogFile->pLogFile, sFormat, ap);

	va_end(ap);

	pstLogFile->lLogCount++;
	fclose(pstLogFile->pLogFile);

	return ShiftFiles(pstLogFile);
}

static unsigned long StrToNum(const char *pStr, int iBase)
{
	char *pEndptr;
	unsigned long ulVal;

	errno = 0;
	ulVal = strtoul(pStr, &pEndptr, iBase);

	if ((errno == ERANGE && (ulVal == ULONG_MAX))
			|| (errno != 0 && ulVal == 0)) {
		return 0;
	}

	if (pEndptr == pStr) {
		return 0;
	}

	return ulVal;
}

static uint32_t crc32_table[256] = {
	0x00000000L, 0xF62B8303L, 0xE13B70F7L, 0x1350F3F4L,
	0xC79A971FL, 0x351F141CL, 0x26A1E7E8L, 0xD4CA64EBL,
	0x8AD958CFL, 0x78BD2BCCL, 0x6BE22838L, 0x9989AB3BL,
	0x4D43CFD0L, 0xBF28C4D3L, 0xAC78BF27L, 0x5E133C24L,
	0x105EC76FL, 0xE235464CL, 0xF165B798L, 0x030E349BL,
	0xD7C45070L, 0x25AFD337L, 0x36FF2087L, 0xC494A384L,
	0x9A879FA0L, 0x68EC1CA3L, 0x7BBCEF57L, 0x89D76C54L,
	0x5D1D08BFL, 0xAF768BBCL, 0xBC267848L, 0x4E4DFB4BL,
	0x20BD8EDEL, 0xD2D60DDDL, 0x1C86FE29L, 0x33ED7D2AL,
	0xE72719C1L, 0x154C9AC2L, 0x016C6936L, 0xF477EA35L,
	0xAA64D611L, 0x580F5512L, 0x4BF5A6E6L, 0xB93425E5L,
	0x6DFE410EL, 0x9F95C20DL, 0x8CC351F9L, 0x7EAEB2FAL,
	0x30E349B1L, 0xC288CAB2L, 0xD1D89346L, 0x23B3BA45L,
	0xF779DEAEL, 0x05125DADL, 0x1642A5E9L, 0xE4292D5AL,
	0xBA3A117EL, 0x4851927DL, 0x5B016198L, 0xA96AE28AL,
	0x7DA08661L, 0x8FCB0562L, 0x9C9BF696L, 0x6EF07595L,
	0x417B1DBCL, 0xB3109EBFL, 0xA0406D4BL, 0x252BEE48L,
	0x86E18AA3L, 0x748A09A0L, 0x67DAFA54L, 0x9B517957L,
	0xCBA24573L, 0x39C9C670L, 0x2A993584L, 0xD82FB687L,
	0x0C38D26CL, 0xFE53516FL, 0xED03A29BL, 0x1F628198L,
	0x5125DAD3L, 0xA34E59D0L, 0xB01EAA24L, 0x42759227L,
	0x96BF4DCCL, 0x64D4CECFL, 0x77843D3BL, 0x85EFB3E8L,
	0xDBFC821CL, 0x2997011FL, 0x3AC7F2EBL, 0xC8AC718EL,
	0x16C61503L, 0xEE0D9600L, 0xFD5D65F4L, 0x0F36E6F7L,
	0x61C69362L, 0x93AD1061L, 0x80FDE395L, 0x72966096L,
	0xA65C047DL, 0x5437877EL, 0x4767748AL, 0xB50CF789L,
	0xEB1FBACDL, 0x197448AEL, 0x0A24BB5AL, 0xF84F3859L,
	0x2C855CB2L, 0xDEEEDFB1L, 0xCDBE2C45L, 0x3FD5AF46L,
	0x719854D1L, 0x83F3D70EL, 0x90A324FAL, 0x62C8A7F9L,
	0xB602C312L, 0x46494011L, 0x5739B3E5L, 0xA55230E6L,
	0xBF410CC2L, 0x09A28FC1L, 0x1A7A7C35L, 0xE811FF36L,
	0x3DCB9BDDL, 0xCEB108DEL, 0xDDE0EB2AL, 0x2F8B6829L,
	0x826F3B78L, 0x709D8B7BL, 0x63CD4B8FL, 0x91A6C88CL,
	0x456CAC67L, 0xB70726F4L, 0xA457DC90L, 0x563C5F93L,
	0x082F36B7L, 0xFA44E04BL, 0xE1941340L, 0x1B7F9043L,
	0xCFB5FA48L, 0x3DDE77ABL, 0x2E8E845FL, 0xDCE5075CL,
	0x92A8FC71L, 0x60C37F14L, 0x73938CE0L, 0x81F80FE3L,
	0x55326B08L, 0xA798EE0BL, 0xB4091BFFL, 0x466298FCL,
	0x1871A4D8L, 0xEA1A27DBL, 0xF94AD42FL, 0x0B21572CL,
	0xDFEB33C7L, 0x2D80B0C4L, 0x3ED04330L, 0xCCBBC033L,
	0xA24BB5A6L, 0x502036A5L, 0x4370C551L, 0xB11B4652L,
	0x65D122B9L, 0x97BAA1BAL, 0x84EA524EL, 0x7681D14DL,
	0x2892ED69L, 0xDAF96E6AL, 0xC9A99D9EL, 0x3BC21E9DL,
	0xEF087A76L, 0x1D63F975L, 0x0E330A81L, 0xFC588982L,
	0xB21572C9L, 0x407EF1CAL, 0x532E023EL, 0xA145813DL,
	0x758FE5D6L, 0x87E466D5L, 0x94B49521L, 0x66DF1622L,
	0x38CC2A06L, 0xCAA7A500L, 0xD9F75AF1L, 0x2B9CD9F2L,
	0xFF56BD19L, 0x0D3D3E1AL, 0x1E6DCDEEL, 0xEC064EEDL,
	0xC38D26C4L, 0x31E6A5C7L, 0x22B65633L, 0xD0DDD530L,
	0x0417B1DBL, 0xF67C32D8L, 0x255CC12CL, 0x1747422FL,
	0x49547E0BL, 0xBB3FFD08L, 0xA86F0EFCL, 0x5A048DFFL,
	0x8ECEE914L, 0x7CA56A17L, 0x6F9559E3L, 0x9D9E1AE0L,
	0xD3D3E1ABL, 0x21B862A8L, 0x32E8915CL, 0xC083125FL,
	0x144976B4L, 0xE622F5B7L, 0xF5720643L, 0x07198540L,
	0x590AB964L, 0xAB613A67L, 0xB8319993L, 0x4A5A4A90L,
	0x9E902E7BL, 0x6CFBAD78L, 0x7FAB5E8CL, 0x8DC0DD8FL,
	0xE330A81AL, 0x115B2B19L, 0x020BD8EDL, 0xF0605BEEL,
	0x24AA3F05L, 0xD6C1BC06L, 0xC5914FF2L, 0x37FACCF1L,
	0x69E9F0D5L, 0x9B8273D6L, 0x88D28022L, 0x7AB90321L,
	0xAE7367CAL, 0x5C18E4C9L, 0x4F48173DL, 0xBD23943EL,
	0xF36E6F75L, 0x0105EC76L, 0x12551F82L, 0xE03E9C81L,
	0x34F4F86AL, 0xC69F7B69L, 0xD5CF889DL, 0x27A40B9EL,
	0x79B737BAL, 0x8BDCB4B9L, 0x9884C74DL, 0x6AE7C44EL,
	0xBE2DA0A5L, 0x4C4623A6L, 0x5F16D052L, 0xAD7D5351L
};

static uint32_t crc_32(uint32_t sed, unsigned char const * data, uint32_t length)
{
	uint32_t crc = sed;
	while (length--) {
		crc = crc32_table[(crc ^ *data++) & 0xFFL] ^ (crc >> 8);
	}

	return crc;
}

#if __WORDSIZE == 64
#define PMAP_ADDR_LEN 12 // 64位地址最多有12个字符
#else
#define PMAP_ADDR_LEN 8 // 32位地址有8个字符
#endif

/*
 * 返回值：
 *      NULL表示没有找到，非NULL表示找到
 */
static char* FindStackSection(const char *pMapStr, long lSP)
{
	char sTmp[32];
	unsigned long lTmpStart, lTmpEnd;
	int iAddrLen = 0;
	char *pAddr = NULL;
	char *pIsFind = NULL;

	do {
        // 栈的权限是rw-p或者rwxp
		pIsFind = strstr((char *)pMapStr, "rw-p"); 
		if (pIsFind) break;
		pIsFind = strstr((char *)pMapStr, "rwxp");
		if (pIsFind) break;
		return NULL;			// 没有找到
	} while (0);

	strncpy(sTmp, "0x", 3);
	pAddr = strstr((char *)pMapStr, "-");
	if (pAddr == NULL) {
		return NULL;
	}
	iAddrLen = pAddr - pMapStr;
	if (iAddrLen > PMAP_ADDR_LEN) {
		return NULL;
	}
	strncat(sTmp, pMapStr, iAddrLen);
	lTmpStart = StrToNum(sTmp, 16);
	if (lTmpStart == 0) {
		return NULL;
	}

	strncpy(sTmp, "0x", 3);
	pAddr++; 
	strncat(sTmp, pAddr, PMAP_ADDR_LEN);
	lTmpEnd = StrToNum(sTmp, 16);
	if (lTmpEnd == 0) {
		return NULL;
	}

	if ((unsigned long)lSP >= lTmpStart && (unsigned long)lSP <= lTmpEnd) {
		return (char *)pMapStr; //找到
	}

	return NULL; 
}

static int GetMemInfo(const char *pMapStr, MemInfo *pstMemInfo, const char *pPath, long lSP)
{
	char *pMapping = NULL;
	char *pIsFind = NULL;
	char sTmp[32];
	unsigned long lTmp;
	int iAddrLen = 0;
	char *pAddr = NULL;

	pMapping = strstr((char *)pMapStr, pPath);
	if (pMapping) { // 找到了进程路径字符串
		do {
            // 数据段的权限是rw-p或者rwxp
			pIsFind = strstr((char *)pMapStr, "rw-p"); 
			if (pIsFind) break;
			pIsFind = strstr((char *)pMapStr, "rwxp");
			if (pIsFind) break;
			return 0;
		} while (0);
		/* 
         * 组成64位地址字符串如0x12345678abcd
         * 组成32位地址字符串如0x12345678
         * 这里需要注意的是64位的地址长度可能是8个字节，前面的
         *
		 * 0省略了，对于32位系统，则一定是8个字节，为了兼容，
		 * 这里不采用定长控制的方式，地址之间一定是以-分割，根据
		 * 这个特征来取地址，以下都一样
		 */
		strncpy(sTmp, "0x", 3);
		pAddr = strstr((char *)pMapStr, "-");
		if (pAddr == NULL) {
			return -99;
		}

		iAddrLen = pAddr - pMapStr;
		if (iAddrLen > PMAP_ADDR_LEN) {
			return -101;
		}

		strncat(sTmp, pMapStr, iAddrLen);

		// 记录数据段的起始地址
		lTmp = StrToNum(sTmp, 16);
		if (lTmp != 0) {
			pstMemInfo->qDataStartAddr = lTmp;
		} else {
			LOG_WITH_TIME("Get Data Section Addr Error:%s", sTmp);
			return -1;
		}

		strncpy(sTmp, "0x", 3);
		// 64位格式如:0x12345678abcd-0x12345678ef00
		// 32位格式如:0x12345678-0x12345690
		pAddr++; // 跳过-

		// 这里64位可能没有12个字节，导致后面的地址非法，但是StrToNum可以自动去除
		strncat(sTmp, pAddr, PMAP_ADDR_LEN);

		// 记录数据段的结束地址
		lTmp = StrToNum(sTmp, 16);
		if (lTmp != 0) {
			pstMemInfo->qDataEndAddr = lTmp;
		} else {
			LOG_WITH_TIME("Get Data Seciont End Addr Error:%s", sTmp);
			return -3;
		}

		// 检查下地址的有效性
		// 这里先记录数据段地址，后面如果找到BSS段地址，再更新结束地址，BSS总是紧跟数据段
		if (pstMemInfo->qDataStartAddr == 0 || pstMemInfo->qDataEndAddr <= pstMemInfo->qDataStartAddr) {
#if __WORDSIZE == 64
			LOG_WITH_TIME("Get DataStartAddr >= DataEndAddr, Start:0x%lx, End:0x%lx",
					pstMemInfo->qDataStartAddr, pstMemInfo->qDataEndAddr);
#else
			LOG_WITH_TIME("Get DatatartAddr >= DataEndAddr, Start:0x%x, End:0x%x",
					(uint32_t)pstMemInfo->qDataStartAddr, (uint32_t)pstMemInfo->qDataEndAddr);
#endif
			return -17;
		}
		//计算长度
		pstMemInfo->dwDataLen = pstMemInfo->qDataEndAddr - pstMemInfo->qDataStartAddr;

		return 0;
	} else {
		pIsFind = FindStackSection(pMapStr, lSP);
		if (pIsFind) { // 找到
			strncpy(sTmp, "0x", 3);
			pAddr = strstr((char *)pMapStr, "-");
			if (pAddr == NULL) {
				return -103;
			}

			iAddrLen = pAddr - pMapStr;
			if (iAddrLen > PMAP_ADDR_LEN) {
				return -105;
			}

			strncat(sTmp, pMapStr, iAddrLen);
			lTmp = StrToNum(sTmp, 16);
			if (lTmp != 0) {
				pstMemInfo->qStackStartAddr = lTmp;
			} else {
				LOG_WITH_TIME("Get Stack Begin Addr Error:%s", sTmp);
				return -9;
			}

			strncpy(sTmp, "0x", 3);
			pAddr++; // 跳过-
			strncat(sTmp, pAddr, PMAP_ADDR_LEN);

			lTmp = StrToNum(sTmp, 16);
			if (lTmp != 0) {
				pstMemInfo->qStackEndAddr = lTmp;
			} else {
				LOG_WITH_TIME("Get Stack End Addr Error:%s", sTmp);
				return -11;
			}

			// 检查下地址的有效性
			if (pstMemInfo->qStackStartAddr == 0 || pstMemInfo->qStackEndAddr <= pstMemInfo->qStackStartAddr) {
#if __WORDSIZE == 64
				LOG_WITH_TIME("Get StackStartAddr >= StackEndAddr, Start:0x%lx, End:0x%lx",
						pstMemInfo->qStackStartAddr, pstMemInfo->qStackEndAddr);
#else
				LOG_WITH_TIME("Get StackStartAddr >= StackEndAddr, Start:0x%x, End:0x%x",
						(uint32_t)pstMemInfo->qStackStartAddr, (uint32_t)pstMemInfo->qStackEndAddr);
#endif
				return -13;
			}

			//计算长度
			pstMemInfo->dwStackLen = pstMemInfo->qStackEndAddr - pstMemInfo->qStackStartAddr;

			return 0;
		} else {
			/*
			 * BSS段肯定是紧挨着数据段，也就是数据段的结束地址等于BSS段的开始地址，根据这个特点来找BSS段的结束地址。
			 * 这里BSS是未初始化或者初始化为0的区域，BSS段和数据段加起来才是整个静态、全局变量区域。
			 */
			do {
				pIsFind = strstr((char *)pMapStr, "rw-p"); // 数据段的权限是rw-p或者rwxp
				if (pIsFind) break;
				pIsFind = strstr((char *)pMapStr, "rwxp");
				if (pIsFind) break;
				return 0;
			} while (0);

			strncpy(sTmp, "0x", 3);
			pAddr = strstr((char *)pMapStr, "-");
			if (pAddr == NULL) {
				return -107;
			}

			iAddrLen = pAddr - pMapStr;
			if (iAddrLen > PMAP_ADDR_LEN) {
				return -109;
			}

			strncat(sTmp, pMapStr, iAddrLen);

			lTmp = StrToNum(sTmp, 16);
			if (lTmp != 0 && pstMemInfo->qDataEndAddr == lTmp) { // 找到了BSS段:BSS段起始地址等于数据段的结束地址
				strncpy(sTmp, "0x", 3);
				pAddr++; // 跳过-
				strncat(sTmp, pAddr, PMAP_ADDR_LEN);

				lTmp = StrToNum(sTmp, 16);
				if (lTmp != 0) {
					pstMemInfo->qDataEndAddr = lTmp;
				} else {
					LOG_WITH_TIME("Get Heap End Addr Error:%s", sTmp);
					return -15;
				}

				// 检查下地址的有效性
				if (pstMemInfo->qDataStartAddr == 0 || pstMemInfo->qDataEndAddr <= pstMemInfo->qDataStartAddr) {
#if __WORDSIZE == 64
					LOG_WITH_TIME("Get BSS DataStartAddr >= DataEndAddr, Start:0x%lx, End:0x%lx",
							pstMemInfo->qDataStartAddr, pstMemInfo->qDataEndAddr);
#else
					LOG_WITH_TIME("Get BSS DatatartAddr >= DataEndAddr, Start:0x%x, End:0x%x",
							(uint32_t)pstMemInfo->qDataStartAddr, (uint32_t)pstMemInfo->qDataEndAddr);
#endif
					return -17;
				}

				pstMemInfo->dwDataLen = pstMemInfo->qDataEndAddr - pstMemInfo->qDataStartAddr;

				return 0;
			}
		}
	}

	return 0;
}

static void* CreateNewShm(int iShmKey, int iSize)
{
	int iShmId;
	void *pShm;

	iShmId = shmget(iShmKey, iSize, IPC_CREAT | IPC_EXCL | 0600);
	if (iShmId == -1) {
		return NULL;
	}

	if ((pShm = shmat(iShmId, NULL, 0)) == (void *) -1) {
		return NULL;
	}

	return pShm;
}

static void* ShmCreate(int iShmKey, int iSize)
{
	int iShmId;
	struct shmid_ds stShmStat;
	int iShmLen;
	void *pShm;

	if (iShmKey == 0) {
		return NULL;
	}

	if ((iShmId = shmget(iShmKey, 0, 0)) == -1) { // 新建SHM
		return (CreateNewShm(iShmKey, iSize));
	}

	if (shmctl(iShmId, IPC_STAT, &stShmStat) < 0) {
		return NULL;
	}

	iShmLen = stShmStat.shm_segsz;
	if (iShmLen == iSize) { // 用原来的SHM
		if ((pShm = shmat(iShmId, NULL, 0)) == (void *) -1) {
			return NULL;
		}
		return pShm;
	}

	//删除原来的SHM，建立新的SHM
	if (shmctl(iShmId, IPC_RMID, NULL)) {
		return NULL;
	}
	return (CreateNewShm(iShmKey, iSize));
}

static int g_iSendToDoRestartFlag = 0;

static char szDealSegvLog[8 * 1024];
static int iDealSegvLogLeft = sizeof(szDealSegvLog);
static int iDealSegvLogLen = 0;
__thread pthread_t g_tTid = (pthread_t)-1;

static void FlushLog()
{
	szDealSegvLog[0] = 0;
	iDealSegvLogLeft = sizeof(szDealSegvLog);
	iDealSegvLogLen = 0;
}

static inline void CallSaveMe(long lSP)
{
	char sMapFilePath[64], sBuffer[1024 + 8];
	FILE *pFile = NULL;
	void *aStack[64];
	int iSize;
	int i;
	void *pstShm = NULL;
	SegvShm *pstSegvShm = NULL;
	MemInfo stMemInfo;
	int iGetMemInfoSucc = 0;

	char sPath[64];
	char sLinkPath[512];
	
	if (0 == lSP) {
		return;
	}

	snprintf(sPath, sizeof(sPath) - 1, "/proc/%d/exe", getpid());
	memset(sLinkPath, 0, sizeof(sLinkPath));
	if(0 > readlink(sPath, sLinkPath, sizeof(sLinkPath))){
		LOG_WITH_TIME("cannot readlink");
	}

	pid_t tPid = getpid();
	memset(&stMemInfo, 0, sizeof(stMemInfo));
	snprintf(sMapFilePath, sizeof(sMapFilePath), "/proc/%d/maps", tPid);
	
	pFile = fopen(sMapFilePath, "r");
	if (NULL != pFile) {
		LOG_WITH_TIME("Memory map Start");
		while (fgets(sBuffer, sizeof(sBuffer) - 8, pFile)) {
			if (iGetMemInfoSucc == 0) {
				iGetMemInfoSucc = GetMemInfo((const char *)sBuffer, &stMemInfo, sLinkPath, lSP);
			}
			DIRCT_LOG("%s", sBuffer);//无条件输出maps文件的内容
		}
		LOG_WITH_TIME("Memory map End");
		fclose(pFile);
	}
	else {
		LOG_WITH_TIME("Read /proc/%d/maps error", tPid);
	}


	// 把core现场写入共享内存
	do {
		static int iOnlyOnceSaveScene = 0;
		if (iOnlyOnceSaveScene++ > 0) {
			break;
		}
		
		// 根据SegvShm的大小动态建立共享内存
		pstShm = ShmCreate(0x20161026, sizeof(SegvShm));
		if (!pstShm) break;
		if (iGetMemInfoSucc != 0) break;
		pstSegvShm = (SegvShm*)pstShm;
		pstSegvShm->qTime = time(NULL);
		strncpy((char *)pstSegvShm->sPath, sLinkPath, sizeof(pstSegvShm->sPath));
		memcpy(&pstSegvShm->stMemInfo, &stMemInfo, sizeof(pstSegvShm->stMemInfo));
		if (pstSegvShm->stMemInfo.dwDataLen > sizeof(pstSegvShm->sData)) {
			pstSegvShm->stMemInfo.dwDataLen = sizeof(pstSegvShm->sData);
		}

		if (pstSegvShm->stMemInfo.dwStackLen > sizeof(pstSegvShm->sStack)) {
			pstSegvShm->stMemInfo.dwStackLen = sizeof(pstSegvShm->sStack);
		}
		
		if (pstSegvShm->stMemInfo.dwDataLen > 0 && pstSegvShm->stMemInfo.qDataStartAddr != 0) {
#if __WORDSIZE == 64
			memcpy((void *)pstSegvShm->sData, (void *)(uint64_t)pstSegvShm->stMemInfo.qDataStartAddr,
					pstSegvShm->stMemInfo.dwDataLen);
			LOG_WITH_TIME("Have Write Data to Shm Succ, Start:0x%12.12lx, End:0x%12.12lx, Len:0x%x",
					pstSegvShm->stMemInfo.qDataStartAddr,
					pstSegvShm->stMemInfo.qDataEndAddr,
					pstSegvShm->stMemInfo.dwDataLen);
#else
			memcpy((void *)pstSegvShm->sData, (void *)(uint32_t)pstSegvShm->stMemInfo.qDataStartAddr,
					pstSegvShm->stMemInfo.dwDataLen);
			LOG_WITH_TIME("Have Write Data to Shm Succ, Start:0x%8.8x, End:0x%8.8x, Len:0x%x",
					(uint32_t)pstSegvShm->stMemInfo.qDataStartAddr,
					(uint32_t)pstSegvShm->stMemInfo.qDataEndAddr,
					pstSegvShm->stMemInfo.dwDataLen);
#endif
		}
		if (pstSegvShm->stMemInfo.dwStackLen > 0 && pstSegvShm->stMemInfo.qStackStartAddr != 0) {
#if __WORDSIZE == 64
			memcpy((void *)pstSegvShm->sStack, (void *)(uint64_t)pstSegvShm->stMemInfo.qStackStartAddr,
					pstSegvShm->stMemInfo.dwStackLen);
			LOG_WITH_TIME("Have Write Stack to Shm Succ, Start:0x%12.12lx, End:0x%12.12lx, Len:0x%x",
					pstSegvShm->stMemInfo.qStackStartAddr,
					pstSegvShm->stMemInfo.qStackEndAddr,
					pstSegvShm->stMemInfo.dwStackLen);
#else
			memcpy((void *)pstSegvShm->sStack, (void *)(uint32_t)pstSegvShm->stMemInfo.qStackStartAddr,
					pstSegvShm->stMemInfo.dwStackLen);
			LOG_WITH_TIME("Have Write Stack to Shm Succ, Start:0x%8.8x, End:0x%8.8x, Len:0x%x",
					(uint32_t)pstSegvShm->stMemInfo.qStackStartAddr,
					(uint32_t)pstSegvShm->stMemInfo.qStackEndAddr,
					pstSegvShm->stMemInfo.dwStackLen);
#endif
		}
	} while(0);

	// 把栈回溯写入日志
	do {
		/*
		 * 在栈被破坏的情况下，这里backtrace可能再次CoreDump
		 * 此时后面的栈回溯记录是无意义的，可以不记录，直接跳过
		 */

		LOG_WITH_TIME("Start to obtained stack frames.");
		static int iOnlyOnceBackTrace = 0;
		if (iOnlyOnceBackTrace++ > 0) { // 这个条件满足则表示在backtrace core掉或者其后面的代码导致了coredump
			LOG_WITH_TIME("backtrace function maybe coredump, iOnlyOnceBackTrace = %d\n", iOnlyOnceBackTrace);
			break;
		}
		
		iSize  = backtrace(aStack, sizeof(aStack) / sizeof(aStack[0]));
		LOG_WITH_TIME("Obtained %d stack frames:", iSize);
		for (i = 0; i < iSize; i++) {
#if __WORDSIZE == 64
			DIRCT_LOG("[%d]:0x%12.12lx\n", i, (uint64_t)aStack[i]);
#else
			DIRCT_LOG("[%d]:0x%8.8x\n", i, (uint32_t)aStack[i]);
#endif
		}
	} while(0);
}

static void ResetSignal(int iSigNo)
{
	switch (iSigNo) {
		case SIGFPE:
		case SIGBUS:
		case SIGABRT:
        case SIGILL:
		case SIGSYS:
		case SIGTRAP:
		case SIGSEGV:
			signal(iSigNo, SIG_DFL); //设置回调为默认处理函数
			raise(iSigNo); //重新发送信号给本线程
			break;
		default:
			exit(1);
	}
}

static long GetContextInfo(int iSigNo, siginfo_t * pstSigInfo, void * pContext, uint32_t tPid, pthread_t tTid)
{
	ucontext_t * pstContext = (ucontext_t *)pContext;
	long lSP = 0;       // Coredump时栈指针内容

	if (pstSigInfo && pstContext){
#if __WORDSIZE == 64
		LOG_WITH_TIME("Meet SIG:%d at 0x%lx:0x%lx, RSP:0x%lx, RBP:0x%lx, (errno:%d code:%d memaddr_ref:0x%lx), RSP of main:0x%lx of %s, pid:%u, tid:%lu(0x%lx)",
				iSigNo,
				pstContext->uc_mcontext.gregs[REG_CSGSFS],
				pstContext->uc_mcontext.gregs[REG_RIP],
				pstContext->uc_mcontext.gregs[REG_RSP],
				pstContext->uc_mcontext.gregs[REG_RBP],
				pstSigInfo->si_errno,
				pstSigInfo->si_code,
				(uint64_t)pstSigInfo->si_addr,
				(uint64_t)g_Main_Stack_SP,
				g_sLinkPath,
				tPid,
				(unsigned long)tTid,
				(unsigned long)tTid,
		);
		lSP = pstContext->uc_mcontext.gregs[REG_RSP];
#else /* __WORDSIZE == 32 */
		LOG_WITH_TIME("Meet SIG:%d at 0x%x:0x%x, ESP:0x%x, EBP:0x%x, (errno:%d code:%d memaddr_ref:0x%x), ESP of main:0x%x of %s, pid:%u, tid:%lu(0x%lx)",
				iSigNo,
				pstContext->uc_mcontext.gregs[REG_CS],
				pstContext->uc_mcontext.gregs[REG_EIP],
				pstContext->uc_mcontext.gregs[REG_ESP],
				pstContext->uc_mcontext.gregs[REG_EBP],
				pstSigInfo->si_errno,
				pstSigInfo->si_code,
				(unsigned int)pstSigInfo->si_addr,
				(uint32_t)g_Main_Stack_SP,
				g_sLinkPath,
				tPid,
				(unsigned long)tTid,
				(unsigned long)tTid,
		);
		lSP = pstContext->uc_mcontext.gregs[REG_ESP];
#endif /* __WORDSIZE == 32 */
	}
	else{
		LOG_WITH_TIME("Meet SIG:%d in %s pid:%u, tid:%lu(0x%lx)", iSigNo, g_sLinkPath, tPid, (unsigned long)tTid, (unsigned long)tTid);
	}
	return lSP;
}

static void DealSegv(int iSigNo, siginfo_t * pstSigInfo, void * pContext)
{
	uint32_t tPid = 0;
	pthread_t tTid = 0;
	static int iOnlyAnalyseOnce = 0;
	static int iLockNum = 0;
	static int iConflict = 0;

	tPid = getpid();
	tTid = pthread_self();

	if (CheckSwichFlag() && CheckStub() && FreqLimit()) {
		static time_t tLastTime = 0;
		time_t tNow = time(NULL);
		if (tNow - tLastTime > 5) {
			tLastTime = tNow;
			GetContextInfo(iSigNo, pstSigInfo, pContext, tPid, tTid);
		}
		LOG_WITH_TIME("prepare to siglongjmp()");

		//只有非多线程才用!!
		siglongjmp(g__bEnv, 1);
		return;
	}

	if (pthread_mutex_trylock(&g_SegvMute)) { //加锁失败
		++iConflict;
		return;
	}
	++iLockNum;//递归锁

	if (tTid == g_tTid || iLockNum > 1) { //重复Coredump发生时候
		LOG_WITH_TIME("Pid %u Tid 0x%lx DealSegv ReEnter\n", tPid, tTid);
	} else {
		g_tTid = tTid;

		fprintf(stderr, "\n\n================ coredump ================\n");\
		
		DIRCT_LOG("\n\n================\n");
		LOG_WITH_TIME("Pid %u Tid 0x%lx DealSegv FirstEnter\n", tPid, tTid);
	}

	if (0 == iOnlyAnalyseOnce) {
		
		iOnlyAnalyseOnce = 1;
		FlushLog();//第一个Coredump调用

		long lSP = GetContextInfo(iSigNo, pstSigInfo, pContext, tPid, tTid); // 打印Coredump时上下文内容

		//CallSaveMe内部处理函数可能还会导致Core，
		//导致CallSaveMe未执行完又间接因为DealSegv调用自己，
		LOG_WITH_TIME("prepare to CallSaveMe()");
		CallSaveMe(lSP);
	}
	
	if (0 == disable_restart_on_core) {
		if(0 == g_iSendToDoRestartFlag) {
			g_iSendToDoRestartFlag = 1;
            // TODO 发包到别的拉起服务的进程，让其将本进程kill掉，然后重启
		}
	}

	while (iLockNum--) {
		pthread_mutex_unlock(&g_SegvMute);
	}

	if (iConflict) { //iConflict>0说明加锁冲突而放弃
		LOG_WITH_TIME("Pid %u Tid 0x%lx Meet DealSegv Conflict=%d\n", tPid, tTid, iConflict);
	}

	//无论是否重复core，都会执行到这里
	ResetSignal(iSigNo); //不要再core进DealSegv来了
}

//pfCheckSwichFlag: 函数指针,非空则使用其来判断是否启用jmp-to-main
int COREDUMP_Init(FCheckSwitchFlag pfCheckSwichFlag, LogFile * pstLog,
		volatile uint32_t * pulAutoStub_first, volatile uint32_t * pulAutoStub_last,
		unsigned long ulMainStackSP)
{
	char sPath[256];
	struct sigaction stAct;
	FILE *pFile = NULL;
	size_t iWriteLen = 0;
	const char *pStr = "1";
	size_t tLen = strlen(pStr);

	g_pfCheckSwichFlag = pfCheckFlag;
	g_uStub_first = pulAutoStub_first;
	g_uStub_last = pulAutoStub_last;
	g_Main_Stack_SP = ulMainStackSP;

	if(pstLog){
		g_pstLog = pstLog;
	}else{
		InitLogFile(&g_stLog, "/data/log/deal_segv", 0, 5, 10000000);
		g_pstLog = &g_stLog;
	}

	//控制Coredump写入哪些段
	memset(sPath, 0, sizeof(sPath));
	snprintf(sPath, sizeof(sPath), "/proc/%d/coredump_filter", getpid());
	pFile = fopen(sPath, "w");
	if (NULL != pFile) {
		iWriteLen = fwrite(pStr, tLen, sizeof(char), pFile);
		fclose(pFile);
		if (iWriteLen != tLen) {
			perror("COREDUMP:write to coredump_filter error.");
		}
	} else {
		printf("COREDUMP:Current Linux can't open coredump_filter(%d)\n",errno);
	}

	//获取程序路径
	memset(sPath, 0, sizeof(sPath));
	snprintf(sPath, sizeof(sPath) - 1, "/proc/%d/exe", getpid());
	memset(g_sLinkPath, 0, sizeof(g_sLinkPath));
	if(0 > readlink(sPath, g_sLinkPath, sizeof(g_sLinkPath))) {
		return -99;
	}

	sigemptyset(&(stAct.sa_mask));
	stAct.sa_flags = SA_SIGINFO;
	stAct.sa_sigaction = DealSegv;
	if(sigaction(SIGSEGV, &stAct, NULL) < 0){
		perror("COREDUMP:sigaction error.");
		return -1;
	}

	if(sigaction(SIGFPE, &stAct, NULL) < 0){
		perror("FPE:sigaction error.");
		return -3;
	}

	if(sigaction(SIGILL, &stAct, NULL) < 0){
		perror("ILL:sigaction error.");
		return -5;
	}

	if(sigaction(SIGBUS, &stAct, NULL) < 0){
		perror("BUS:sigaction error.");
		return -9;
	}

	if(sigaction(SIGSYS, &stAct, NULL) < 0){
		perror("SYS:sigaction error.");
		return -13;
	}

	if(sigaction(SIGTRAP, &stAct, NULL) < 0){
		perror("TRAP:sigaction error.");
		return -15;
	}

	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&g_SegvMute, &attr);

	return 0;
}


