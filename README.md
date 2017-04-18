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
