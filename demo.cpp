#include "segv_api.h"

int main(int argc, char* argv[])
{
    SEGV_DECLARE_FIRST
	//balabala
    SEGV_DECLARE_LAST

    
    SEGV_TREAT(NULL, NULL);

	//Do someting

    SEGV_TLINUX_MAIN_END

    return 0;
}
