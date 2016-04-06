#include <pthread.h>

#include "utils/dbpool/dbpool.h"
#include "utils/ipconnector/ipconnector.h"
#include "utils/timemeasurer/timemeasurer.h"
#include "utils/coacommon.h"

struct SMainConfig {
	char	*m_pszPidFile;
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
int CreateNASList (std::map<std::string,std::string> *p_pmapNASList);
void ChangeOSUser ();
int ConnectCoASensor (CIPConnector &p_coIPConn);
int RequestTimer();
int MainWork ();

int CreateSubscriberList (std::vector<SSubscriberRefresh> *p_pvectRefresh);

struct SSessionInfo {
	std::string m_strUserName;
	std::string m_strNASIPAddress;
	std::string m_strSessionId;
	std::string m_strLocation;
};

struct SSessionInfoFull {
	SSessionInfo m_soSessInfo;
	std::string m_strServiceInfo;
};

bool operator < (const SSessionInfo &, const SSessionInfo &);
struct SPolicyInfo {
	std::string m_strUserName;
	std::string m_strLocation;
};
bool operator < (const SPolicyInfo &, const SPolicyInfo &);
struct SPolicyDetail {
	std::string m_strAttr;
	std::string m_strValue;
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
int CreateSessionList(const char *p_pcszSubscriberID, std::map<SSessionInfo, std::map<std::string, int> > *p_pmapSessList, otl_connect &p_coDBConn);
int CreateSessionListFull(std::multimap<std::string,SSessionInfoFull> &p_mmapSessList);
int GetNASLocation(std::string &p_strNASIPAddress, std::string &p_strLocation);
int CreatePolicyList (const SSessionInfo *p_pcsoSessInfo, std::map<SPolicyInfo,std::map<SPolicyDetail,int> > *p_pmapPolicy, otl_connect &p_coDBConn);
int ModifyName (const char *p_pszModifyRule, std::string &p_strLocation, std::string &p_strValue);
bool Filter (const char *p_pszFilterName, std::string &p_strLocation, std::string &p_strValue);
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
