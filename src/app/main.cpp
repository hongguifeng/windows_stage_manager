#ifdef _WIN32

#include "app/app_lifecycle.h"

#include <windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command)
{
    stage_manager::app::AppLifecycle lifecycle;
    return lifecycle.run(instance, show_command);
}

#else

int main()
{
    return 0;
}

#endif
