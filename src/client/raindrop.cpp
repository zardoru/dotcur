#include <exception>
#include <atomic>
#include <memory>
#include <string>
#include <filesystem>

#include "structure/Screen.h"
#include "Application.h"


// ErrorReporting.cpp
void RegisterSignals();

#ifdef _WIN32
#include <windows.h>
void PrintTraceFromContext(CONTEXT &ctx);
#endif

void RunApplication(int argc, char** argv) {
	Application App(argc, argv);
	App.Init();
	App.Run();
	App.Close();
}

int main(int argc, char *argv[])
{
	RegisterSignals();
	
#if _WIN32 && NDEBUG
	int cd = 0;
	_EXCEPTION_POINTERS *ex_info;
	__try {
#endif
		RunApplication(argc, argv);
#if _WIN32 && NDEBUG
	} 
	__except ( ex_info = GetExceptionInformation(),
		GetExceptionCode() == STATUS_ACCESS_VIOLATION ||
		GetExceptionCode() == STATUS_ARRAY_BOUNDS_EXCEEDED) {
		PrintTraceFromContext(*ex_info->ContextRecord);

		MessageBox(nullptr, 
			L"raindrop has crashed. Please see the log for details and maybe send it to github with a report of what you were doing to try and fix it.",
			L"Crash!", MB_OK | MB_ICONERROR);
	}
#endif

    return 0;
}