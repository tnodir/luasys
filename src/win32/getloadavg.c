/* Lua System: Win32: loadavg() */

#if defined(SRWLOCK_INIT) && defined(WIN32_VISTA)

#define filetime_int64(ft)	((int64_t) ((LARGE_INTEGER *) &(ft))->QuadPart)

static struct {
  int64_t idle;
  int64_t kernel;
  int64_t user;
} g_OldTimes;


static int
getloadavg (double *loadavg)
{
  int res = -1;

  EnterCriticalSection(&g_CritSect);
  {
    FILETIME idleTime, kernelTime, userTime;

    if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
      const int64_t idle = filetime_int64(idleTime);
      const int64_t kernel = filetime_int64(kernelTime);
      const int64_t user = filetime_int64(userTime);

      const int64_t idle_delta = idle - g_OldTimes.idle;
      const int64_t kernel_delta = kernel - g_OldTimes.kernel;
      const int64_t user_delta = user - g_OldTimes.user;

      *loadavg = 1.0 - (double) idle_delta / (kernel_delta + user_delta);

      g_OldTimes.idle = idle;
      g_OldTimes.kernel = kernel;
      g_OldTimes.user = user;

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
