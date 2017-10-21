# CoreDumpTool
a tool for CoreDump
- 提供coredump自动重启功能（建议发包到另一个程序让其重启coredump进程）
- 提供coredump自动jump到main函数功能（慎用）
- coredump时将上下文信息保存（信号编号、cs／ip／sp／标配寄存器值、pid／tid、signinfo\_t、maps、backtrace调用栈）
- 提供core\_tool.sh脚本打印调用栈（进程需－g编译）


## how to use
 - demo.log (the file will be create when a coredump occur)
 - demo.cpp (show you how to use it)
``` C
#include "core_api.h"

int main(int argc, char* argv[])
{
    COREDUMP_DECLARE_FIRST
	//balabala
    COREDUMP_DECLARE_LAST

    
    COREDUMP_TREAT(NULL, NULL);

	//Do someting

    return 0;
}
```

## shell
``` shell
 ./core\_tool.sh bt demo.log
```


## 原理知识点
int getcontext(ucontext_t *ucp) 该函数用来将当前执行状态上下文保存到一个ucontext_t结构中，若后续调用setcontext或swapcontext恢复该状态，则程序会沿着getcontext调用点之后继续执行，看起来好像刚从getcontext函数返回一样。 这个操作的功能和setjmp所起的作用类似，都是保存执行状态以便后续恢复执行，但需要重点指出的是：getcontext函数的返回值仅能表示本次操作是否执行正确，而不能用来区分是直接从getcontext操作返回，还是由于setcontext/swapcontex恢复状态导致的返回，这点与setjmp是不一样的

sigsetjmp()会保存目前堆栈环境，然后将目前的地址作一个记号，而在程序其他地方调用siglongjmp()时便会直接跳到这个记号位置，然后还原堆栈，继续程序的执行。当sigsetjmp()返回0时代表已经做好记号上，若返回非0则代表由siglongjmp（）跳转回来。

控制Coredump写入哪些段
/proc/%d/coredump_filter
- (bit 0) anonymous private memory（匿名私有内存段）
- (bit 1) anonymous shared memory（匿名共享内存段）
- (bit 2) file-backed private memory（file-backed 私有内存段）
- (bit 3) file-backed shared memory（file-bakced 共享内存段）
- (bit 4) ELF header pages in file-backed private memory areas (it is effective only if the bit 2 is cleared)（ELF 文件映射，只有在bit 2 复位的时候才起作用）
- (bit 5) hugetlb private memory（大页面私有内存）
- (bit 6) hugetlb shared memory（大页面共享内存）
http://stackoverflow.com/questions/8836459/minimal-core-dump-stack-trace-current-frame-only

打印信息
相关堆栈信息：
iSigNo//信号编号
REG_CSGSFS\REG_CS	//CS段
REG_RIP\REG_EIP 		//IP寄存器
REG_RSP\REG_ESP 		//coredump时的栈寄存器
REG_RBP\REG_EBP 		//基址寄存器
pstSigInfo->si_errno		//siginfo_t结构
pstSigInfo->si_code 		//信号产生的原因
pstSigInfo->si_addr 		//触发的内存地址，对SIGILL,SIGFPE,SIGSEGV,SIGBUS 信号有意义
Main函数的SP值
进程函数名
Pid
Tid//线程id

maps
http://stackoverflow.com/questions/1401359/understanding-linux-proc-id-maps

addr2line
https://linux.die.net/man/1/addr2line

https://www.ibm.com/developerworks/cn/linux/l-ipc/part2/index1.html
https://www.ibm.com/developerworks/cn/linux/l-ipc/part2/index2.html
http://www.mkssoftware.com/docs/man5/siginfo_t.5.asp
