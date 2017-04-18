#ifndef __COREDUMP_API_H__
#define __COREDUMP_API_H__



#include <stdint.h>
#include <time.h>
#include <setjmp.h>
#include <ucontext.h>

//׮ֵ
#define COREDUMP_VAL_STUB 0xabcd1234

#define MAX_THREAD_NUM 10000
#define THREAD_HASH_MODES 9973

// Data����Stack���Ĵ�С
#define MAX_DATA_BUFFER 12*1024*1024
#define MAX_STACK_BUFFER 12*1024*1024

#pragma pack(1)

typedef struct {
	uint64_t qDataStartAddr; // Data���׵�ַ
	uint64_t qDataEndAddr;   // Data��������ַ
	uint32_t dwDataLen;      // Data�����ݴ�С
	
	uint64_t qStackStartAddr;// Stack���׵�ַ
	uint64_t qStackEndAddr;  // Stack��������ַ
	uint32_t dwStackLen;     // Stack�����ݴ�С
} MemInfo;

typedef struct {
	uint64_t qTime;                   // д�����ݵ�ʱ��
	uint8_t sPath[1024];              // ����·��
	MemInfo stMemInfo;
	uint8_t sData[MAX_DATA_BUFFER];   // ȫ�ֱ��� & ��̬����
	uint8_t sStack[MAX_STACK_BUFFER]; // ջ�ռ�
} SegvShm;
#pragma pack()


typedef struct {
	FILE    *pLogFile;
	char    sBaseFileName[80];
	char    sLogFileName[80];
	int     iShiftType;// 0 -> shift by size,  1 -> shift by LogCount, 2 -> shift by interval, 3 ->shift by day, 4 -> shift by hour, 5 -> shift by min
	int     iMaxLogNum;
	int32_t    lMaxSize;
	int32_t    lMaxCount;
	int32_t    lLogCount;
	time_t     lLastShiftTime;
} LogFile;


typedef int (*FCheckSwitchFlag)();

#ifdef  __cplusplus
extern "C" {
#endif

int COREDUMP_Init(FCheckSwitchFlag pfCheckFlag,
				LogFile * pstLog, 
				volatile uint32_t * pulAutoStub_first,
				volatile uint32_t * pulAutoStub_last,
				unsigned long ulMainStackSP);

#ifdef  __cplusplus
}
#endif

//׮:�Զ�����1, sBlankGap �𱣻�����
#define COREDUMP_DECLARE_FIRST \
	volatile char v__sBlankGap_first[1024];\
	volatile uint32_t v__ulStub_first = COREDUMP_VAL_STUB;\


//׮:�Զ�����2
#define COREDUMP_DECLARE_LAST \
	volatile uint32_t v__ulStub_last = COREDUMP_VAL_STUB;\
	volatile char v__sBlankGap_last[1024];\
	ucontext_t __stMainContext, *__pstMainContext = &__stMainContext; \
	unsigned long __ulMainStackSP = 0; \

extern sigjmp_buf g__bEnv;

#if __WORDSIZE == 64 // for 64 bit sys
#define COREDUMP_TREAT(pfunc, plog) do{\
	v__sBlankGap_first[0] = 0; /*use it to avoid compiling warning.*/\
	v__sBlankGap_last[0] = 0; /*use it to avoid compiling warning.*/\
	if (0 == getcontext(__pstMainContext)) { \
		__ulMainStackSP = __pstMainContext->uc_mcontext.gregs[REG_RSP]; \
	} \
	if(0 == sigsetjmp(g__bEnv, 1)){\
		COREDUMP_Init(pfunc, NULL, &v__ulStub_first, &v__ulStub_last, __ulMainStackSP);\
	}else{\
	}\
}while(0);

#else //end  #if __WORDSIZE == 64

#define COREDUMP_TREAT(pfunc, plog) do{\
	v__sBlankGap_first[0] = 0; /*use it to avoid compiling warning.*/\
	v__sBlankGap_last[0] = 0; /*use it to avoid compiling warning.*/\
	if (0 == getcontext(__pstMainContext)) { \
		__ulMainStackSP = __pstMainContext->uc_mcontext.gregs[REG_ESP]; \
	} \
	if(0 == sigsetjmp(g__bEnv, 1)){\
		COREDUMP_Init(pfunc, NULL, &v__ulStub_first, &v__ulStub_last, __ulMainStackSP);\
	}else{\
	}\
}while(0);

#endif //end #if __WORDSIZE == 32



#endif // __COREDUMP_API_H__
