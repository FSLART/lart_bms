#include "main.h"
#include "uartDMA.h"

#define SOFTWARE_CODENAME bms_lart
#define SOFTWARE_VERSION alpha-v2

#define HARDWARE_VERSION "????"
#define HARDWARE_CODENAME chicote

#define PROJECT_LINK "github.com/EsTaNG9/lart_bms"

// ---- helpers to stringify bare identifiers ----
#define STR_HELPER(x) #x
#define STR(x)        STR_HELPER(x)

void startUI(void) {

	static const char info_json[] =
	  "[{\"startui\":{"
	    "\"software\":{\"codename\":\"" STR(SOFTWARE_CODENAME) "\","
	                   "\"version\":\""  STR(SOFTWARE_VERSION)  "\"},"
	    "\"hardware\":{\"version\":\""  HARDWARE_VERSION       "\","
	                   "\"codename\":\"" STR(HARDWARE_CODENAME) "\"},"
	    "\"project_link\":\""           PROJECT_LINK           "\""
	  "}}]";

	printfDma("%s", info_json);
	printfDma("\r\n");
}
