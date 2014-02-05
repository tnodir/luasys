/* Lua System: Win32: loadavg() */

#ifdef WIN32_VISTA

#define filetime_int64(ft)	*((int64_t *) &(ft))

struct win32_times {
  FILETIME idle;
  FILETIME kernel;
  FILETIME user;
};

static struct win32_times g_OldTimes;


static int
getloadavg (double *loadavg)
{
  int res = -1;

  EnterCriticalSection(&g_CritSect);
  {
    struct win32_times times;

    if (GetSystemTimes(&times.idle, &times.kernel, &times.user)) {
      const int64_t usr = filetime_int64(times.user) - filetime_int64(g_OldTimes.user);
      const int64_t kerl = filetime_int64(times.kernel) - filetime_int64(g_OldTimes.kernel);
      const int64_t idl = filetime_int64(times.idle) - filetime_int64(g_OldTimes.idle);
      const int64_t sys = kerl + usr;

      *loadavg = 1.0 - (double) idl / sys;

      g_OldTimes = times;
      res = 0;
    }
  }
  LeaveCriticalSection(&g_CritSect);

  return res;
}

#else

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

#endif /* !WIN32_VISTA */
