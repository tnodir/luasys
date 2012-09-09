/* Lua System: Win32: loadavg() */

typedef struct {
  DWORD status;
  union {
    LONG vLong;
    double vDouble;
    LONGLONG vLongLong;
    void *vPtr;
  } u;
} PDH_VALUE;

typedef long (WINAPI *PPdhOpenQuery) (LPCSTR, DWORD_PTR, HANDLE *);
typedef long (WINAPI *PPdhAddEnglishCounter) (HANDLE, LPCSTR, DWORD_PTR, HANDLE *);
typedef long (WINAPI *PPdhCollectQueryData) (HANDLE);
typedef long (WINAPI *PPdhGetFormattedCounterValue) (HANDLE, DWORD, LPDWORD, PDH_VALUE *);

#define PDH_CPU_QUERY	"\\Processor(_Total)\\% Processor Time"


static HINSTANCE hdll;
static HANDLE hquery;
static HANDLE hcounter;

static PPdhOpenQuery pPdhOpenQuery;
static PPdhAddEnglishCounter pPdhAddEnglishCounter;
static PPdhCollectQueryData pPdhCollectQueryData;
static PPdhGetFormattedCounterValue pPdhGetFormattedCounterValue;


static int
getloadavg (double *loadavg)
{
  PDH_VALUE value;
  int res;

  if (!hdll) {
    hdll = LoadLibrary("pdh.dll");
    if (!hdll) return -1;

    pPdhOpenQuery = (PPdhOpenQuery)
     GetProcAddress(hdll, "PdhOpenQueryA");
    pPdhAddEnglishCounter = (PPdhAddEnglishCounter)
     GetProcAddress(hdll, "PdhAddEnglishCounterA");
    pPdhCollectQueryData = (PPdhCollectQueryData)
     GetProcAddress(hdll, "PdhCollectQueryData");
    pPdhGetFormattedCounterValue = (PPdhGetFormattedCounterValue)
     GetProcAddress(hdll, "PdhGetFormattedCounterValue");

    res = pPdhOpenQuery(NULL, 0, &hquery);
    if (res) return res;

    res = pPdhAddEnglishCounter(hquery, PDH_CPU_QUERY, 0, &hcounter);
    if (res) return res;

    pPdhCollectQueryData(hquery);  /* to avoid PDH_INVALID_DATA result */
  }

  if (!hcounter) return -1;

  res = pPdhCollectQueryData(hquery);
  if (res) return res;

  res = pPdhGetFormattedCounterValue(hcounter, 0x8200, NULL, &value);
  if (res) return res;

  *loadavg = value.u.vDouble / 100.0;
  return 0;
}
