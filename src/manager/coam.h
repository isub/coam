#include <pthread.h>

#include "utils/dbpool/dbpool.h"
#include "utils/ipconnector/ipconnector.h"
#include "utils/timemeasurer/timemeasurer.h"
#include "utils/coacommon.h"

struct SMainConfig {
	char	*m_pszPidFile;
	int		m_iDCD;			/* Debug Call Depth */
};

/* ���������� � ������ � ������� ���������� ������� */
struct SSubscriberRefresh {
	char m_mcSubscriberId[64];
	char m_mcRefreshDate[32];
	char m_mcAction[32];
};

/* ���������� ��� ������ ������ */
struct SThreadInfo {
	SSubscriberRefresh m_soSubscriberRefresh;	/* ���������, ���������� ���������� � ������ � ������� ���������� ������� */
	int m_iBusy;								/* ������� ��������� ������ */
	int m_iExit;								/* ������� ���������� ������ ������ */
	int m_iRetVal;								/* ��� ���������� ������ */
	pthread_mutex_t m_tMutex;					/* ������ ������������� ������ */
	pthread_t m_tThreadId;						/* ���������� ������ */
	CIPConnector *m_pcoIPConn;					/* ��������� ������� ������ ��� �������������� � CoASensd */
};

/* ������������� ������� */
int InitThreadPool ();
/* ������������� ������ */
int InitThread (SThreadInfo &p_soThreadInfo);
/* ���������� ������ ������� */
void DeInitThreadPool ();
/* ������������ �������� ������ */
void CleanUpThreadInfo (SThreadInfo *p_psoThreadInfo);
/* ������������� ����� ����� �������� */
int ThreadManager (const SSubscriberRefresh &p_soRefreshRecord);
/* ��������� ������ */
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

/* ��������� �������� ������ ���������� */
int OperateSubscriber (const SSubscriberRefresh &p_soRefreshRecord, CIPConnector *p_pcoIPConn, otl_connect &p_coDBConn);
/* ��������� ������ ���������� */
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
/* ������� �� �� ������ �������� ������ ���������� */
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
