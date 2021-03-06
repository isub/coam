#include <pthread.h>
#include <string>

#include "utils/dbpool/dbpool.h"
#include "utils/ipconnector/ipconnector.h"
#include "utils/timemeasurer/timemeasurer.h"
#include "utils/coacommon.h"
#include "utils/ps_common.h"

#define ID_TYPE_SESSION_ID		"session_id"
#define ID_TYPE_SUBSCRIBER_ID	"subscriber_id"
#define ID_TYPE_UNAME_FIPA_NAS	"uNameFrIPAddrNAS" /* <userName>\t<framedIPAddress>\t<NASIPAddress> */

#define ACTION_TYPE_LOGOFF			"logoff"
#define ACTION_TYPE_CHECK_SESS		"checksession"
#define ACTION_TYPE_CHECK_POLICY	"checkpolicy"
#define ACTION_TYPE_REAUTHORIZE		"reauth"

#define RESULT_SESSION_FIXED	(32768)

#define LOG_MIN_QUEUE_SIZE	100

struct SMainConfig {
	char	*m_pszPidFile;
	int		m_iDCD;			/* Debug Call Depth */
};

/* информация о записи в очереди обновления политик */
struct SRefreshRecord {
	std::string		m_strIdentifierType;
	std::string		m_strIdentifier;
	std::string		m_strRowId;
	std::string		m_strAction;
	otl_datetime	m_coDateTime;
};

/* информация для работы потока */
struct SThreadInfo {
	SRefreshRecord m_soRefreshRecord;		/* структура, содержащая информацию о записи в очереди обновления политик */
	int m_iBusy;							/* признак занятости потока */
	int m_iExit;							/* признак завершения работы потока */
	int m_iRetVal;							/* код завершения потока */
	pthread_mutex_t m_tMutex;				/* объект синхронизации потока */
	pthread_t m_tThreadId;					/* дескриптор потока */
	CIPConnector *m_pcoIPConn;				/* указатель объекта класса для взаимодействия с coas */
	SThreadInfo() : m_iBusy( 0 ), m_iExit( 0 ), m_iRetVal( 0 ), m_tThreadId( 0 ), m_pcoIPConn( NULL ) {}
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
int ThreadManager (const SRefreshRecord &p_soRefreshRecord);
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

int loadRefreshQueue (std::vector<SRefreshRecord> *p_pvectRefresh);

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
int OperateRefreshRecord (const SRefreshRecord &p_soRefreshRecord, CIPConnector *p_pcoIPConn, otl_connect &p_coDBConn);

/* модификация имен активных сервисов и включение сессии в список */
int OperateSessionInfo( std::map<SSessionInfo, std::map<std::string, int> > *p_pmapSessList, SSessionInfo &p_soSessInfo, std::string *p_pstrServiceInfo );

/* обработка сессии подписчика */
int OperateSubscriberSession(
	const SRefreshRecord &p_soRefreshRecord,
	const SSessionInfo &p_soSessionInfo,
	std::map<std::string, int> &p_soSessionPolicyList,
	std::map<SPolicyInfo, std::map<SPolicyDetail, int> > &p_mapProfilePolicyList,
	CIPConnector &p_coIPConn,
	otl_connect &p_coDBConn,
	std::string &p_strWhatWasDone,
	bool &p_bWriteLog );
int DeactivateNotrelevantPolicy( const SSessionInfo &p_soSessionInfo,
								 std::map<std::string, int> &p_soSessionPolicyList,
								 CIPConnector &p_coIPConn,
								 std::string &p_strWhatWasDone );
int ActivateInactivePolicy(
	const SSessionInfo &p_soSessionInfo,
	std::map<SPolicyDetail, int> &p_mapPolicyDetail,
	CIPConnector &p_coIPConn,
	std::string &p_strWhatWasDone );
/* выборка из БД списка активных сессий подписчика */
int CreateSessionList(const std::string &p_strSubscriberID, std::map<SSessionInfo, std::map<std::string, int> > *p_pmapSessList, otl_connect &p_coDBConn);
int CreateSessionListFull(std::multimap<std::string,SSessionInfoFull> &p_mmapSessList);
int CreatePolicyList (const SSessionInfo *p_pcsoSessInfo, std::map<SPolicyInfo,std::map<SPolicyDetail,int> > *p_pmapPolicy, otl_connect &p_coDBConn);
int ModifyName (const char *p_pszModifyRule, std::string &p_strLocation, std::string &p_strValue);
bool Filter (const char *p_pszFilterName, std::string &p_strLocation, std::string &p_strValue);
int SelectActualPolicy (std::map<std::string,int> *p_pmapSessionDetail, std::map<SPolicyDetail,int> *p_pmapPolicyDetail);
int SetCommonCoASensorAttr (SPSRequest *p_psoRequest, size_t p_stBufSize, const SSessionInfo &p_soSessionInfo, CConfig *p_pcoConf, CIPConnector *p_pcoIPConn);
int DeActivateService( const SSessionInfo &p_soSessInfo,
					   const char *p_pcszServiceInfo,
					   const char *p_pcszAttr,
					   CIPConnector *p_pcoIPConn,
					   std::string &p_strWhatWasDone );
int ActivateService(
	const SSessionInfo &p_soSessInfo,
	const char *p_pcszServiceInfo,
	const char *p_pcszAttr,
	CIPConnector *p_pcoIPConn,
	std::string &p_strWhatWasDone );
int AccountLogoff( const SSessionInfo &p_soSessInfo, CIPConnector *p_pcoIPConn, std::string &p_strWhatWasDone );
int CheckSession( const SSessionInfo &p_soSessInfo, CIPConnector *p_pcoIPConn, otl_connect &p_coDBConn, std::string &p_strWhatWasDone );
int FixStuckSession (const SSessionInfo &p_soSessInfo, otl_connect &p_coDBConn, bool p_bOpt = false);

int ReAuthorize( const SSessionInfo &p_soSessInfo, const std::string & p_strFramedIPAddress, CIPConnector *p_pcoIPConn, std::string &p_strWhatWasDone );

int ParsePSPack (const SPSRequest *p_pcsoResp, size_t p_stRespLen, int p_iFindResult = 1);
int DeleteRefreshRecord (const SRefreshRecord *p_pcsoSubscr, otl_connect &p_coDBConn);
