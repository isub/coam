#include <pthread.h>

#define OTL_ORA11G
#include "utils/otlv4.h"
#include "utils/ipconnector/ipconnector.h"
#include "utils/coacommon.h"

extern otl_connect g_coMainDBConn;
extern char g_mcDebugFooter[256];

#define FOOT(a)	&(g_mcDebugFooter[sizeof(g_mcDebugFooter)-((a)>=sizeof(g_mcDebugFooter) ? sizeof(g_mcDebugFooter) - 1 : (a+1))])

/* макрос для логирования входа в функцию в режиме отладки */
#define	ENTER_ROUT	if (1 <= g_soMainCfg.m_iDebug) { \
		g_coLog.WriteLog ("%sdebugL1: %s started", FOOT(g_soMainCfg.m_iDCD), __func__); \
		++g_soMainCfg.m_iDCD; \
	}

/* макрос для логирования выхода из функции в режиме отладки */
#define LEAVE_ROUT(retCode)		if (1 <= g_soMainCfg.m_iDebug) { \
		--g_soMainCfg.m_iDCD; \
		g_coLog.WriteLog ("%sdebugL1: %s finished. Result code: '%d'", FOOT(g_soMainCfg.m_iDCD), __func__, retCode); \
	}

/* */
#define DEBUG3	if (3 <= g_soMainCfg.m_iDebug) { \
		g_coLog.WriteLog ("%sdebugL3: %s:%d ", FOOT(g_soMainCfg.m_iDCD), __func__, __LINE__); \
	}


struct SMainConfig {
	char	*m_pszPidFile;
	int		m_iDebug;		/* Debug level */
	int		m_iDCD;			/* Debug Call Depth */
};

/* информация о записи в очереди обновления политик */
struct SSubscriberRefresh {
	char m_mcSubscriberId[64];
	char m_mcRefreshDate[32];
	char m_mcAction[32];
};

/* информация для работы потока */
struct SThreadInfo {
	SSubscriberRefresh m_soSubscriberRefresh;	/* структура, содержащая информацию о записи в очереди обновления политик */
	int m_iBusy;								/* признак занятости потока */
	int m_iExit;								/* признак завершения работы потока */
	int m_iRetVal;								/* код завершения потока */
	pthread_mutex_t m_tMutex;					/* объект синхронизации потока */
	pthread_t m_tThreadId;						/* дескриптор потока */
	otl_connect *m_pcoDBConn;					/* указатель объекта класса для взаимодействия с БД */
	CIPConnector *m_pcoIPConn;					/* указатель объекта класса для взаимодействия с CoASensd */
};

/* инициализация потоков */
int InitThreadPool ();
/* инициализация потока */
int InitThread (SThreadInfo &p_soThreadInfo);
/* завершение работы потоков */
void DeInitThreadPool ();
/* освобождение ресурсов потока */
void CleanUpThreadInfo (SThreadInfo *p_psoThreadInfo);
/* распределение задач между потоками */
int ThreadManager (const SSubscriberRefresh &p_soRefreshRecord);
/* процедура потока */
void *ThreadWorker (void *p_pvParam);

int LoadConf (const char *p_pszConfDir);
int InitCoAManager();
int DeInitCoAManager();
int ConnectDB (otl_connect &p_coDBConn);
int CreateNASList (std::map<std::string,std::string> *p_pmapNASList);
void ChangeOSUser ();
int ConnectCoASensor (CIPConnector &p_coIPConn);
int RequestTimer();
int MainWork ();

int CreateSubscriberList (std::vector<SSubscriberRefresh> *p_pvectRefresh);

struct SSessionInfo {
	char m_mcUserName[128];
	char m_mcNasIPAddress[16];
	char m_mcCiscoParentSessionId[64];
	char m_mcLocation[128];
};

bool operator < (const SSessionInfo &, const SSessionInfo &);
struct SPolicyInfo {
	char m_mcUserName[128];
	char m_mcLocation[128];
};
bool operator < (const SPolicyInfo &, const SPolicyInfo &);
struct SPolicyDetail {
	char m_mcAttr[32];
	char m_mcValue[256];
};
bool operator < (const SPolicyDetail &, const SPolicyDetail &);

/* обработка полтитик одного подписчика */
int OperateSubscriber (const SSubscriberRefresh &p_soRefreshRecord, CIPConnector *p_pcoIPConn, otl_connect &p_coDBConn);
/* обработка сессии подписчика */
int OperateSubscriberSession (
	const SSubscriberRefresh &p_soRefreshRecord,
	const SSessionInfo &p_soSessionInfo,
	std::map<std::string,int> &p_soSessionPolicyList,
	std::map<SPolicyInfo,std::map<SPolicyDetail,int> > &p_mapProfilePolicyList,
	CIPConnector &p_coIPConn,
	otl_connect &p_coDBConn);
int DeactivateNotrelevantPolicy (const SSubscriberRefresh &p_soRefreshRecord, const SSessionInfo &p_soSessionInfo, std::map<std::string,int> &p_soSessionPolicyList, CIPConnector &p_coIPConn);
int ActivateInactivePolicy (
	const SSubscriberRefresh &p_soRefreshRecord,
	const SSessionInfo &p_soSessionInfo,
	std::map<SPolicyDetail,int> &p_mapPolicyDetail,
	CIPConnector &p_coIPConn);
/* выборка из БД списка активных сессий подписчика */
int CreateSessionList (const char *p_pcszSubscriberID, std::map<SSessionInfo,std::map<std::string,int> > *p_pmapSessList, otl_connect &p_coDBConn);
int GetNASLocation (char *p_pszNASName, char *p_pszLocation, int p_iBufSize);
int CreatePolicyList (const SSessionInfo *p_pcsoSessInfo, std::map<SPolicyInfo,std::map<SPolicyDetail,int> > *p_pmapPolicy, otl_connect &p_coDBConn);
int ModifyName (const char *p_pszModifyRule, char *p_pszLocation, char *p_pszValue);
bool Filter (const char *p_pszFilterName, char *p_pszLocation, char *p_pszValue);
int SelectActualPolicy (std::map<std::string,int> *p_pmapSessionDetail, std::map<SPolicyDetail,int> *p_pmapPolicyDetail);
int SetCommonCoASensorAttr (SPSRequest *p_psoRequest, size_t p_stBufSize, const SSessionInfo *p_pcsoSessionInfo, CConfig *p_pcoConf, CIPConnector *p_pcoIPConn);
int DeActivateService (
	const SSessionInfo *p_pcsoSessInfo,
	const char *p_pcszServiceInfo,
	const char *p_pcszAttr,
	CIPConnector *p_pcoIPConn);
int ActivateService (
	const SSessionInfo *p_pcsoSessInfo,
	const char *p_pcszServiceInfo,
	const char *p_pcszAttr,
	CIPConnector *p_pcoIPConn);
int AccountLogoff (
	const SSessionInfo *p_pcsoSessInfo,
	CIPConnector *p_pcoIPConn);
int CheckSession (const SSessionInfo *p_pcsoSessInfo, CIPConnector *p_pcoIPConn, otl_connect &p_coDBConn);
int FixStuckSession (const SSessionInfo *p_pcsoSessInfo, otl_connect &p_coDBConn, bool p_bOpt = false);
int ParsePSPack (const SPSRequest *p_pcsoResp, size_t p_stRespLen, int p_iFindResult = 1);
int DeleteRefreshRecord (const SSubscriberRefresh *p_pcsoSubscr, otl_connect &p_coDBConn);

/* дополнительные операции с БД */
/* попытка переподключения к БД.
   в случе успешного переподключения возвращает 0
   в случаях неудачи -200001 */
int ReconnectDB (otl_connect &p_coDBConn);
/* проверка подключения к БД, в случае если подклчиюение работоспособно возвращает '0' */
int CheckDBConnection (otl_connect &p_coDBConn);
/* завершение текущего подключения к БД */
void DisconnectDB (otl_connect &p_coDBConn);
