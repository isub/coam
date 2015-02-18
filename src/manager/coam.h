#include <pthread.h>

#define OTL_ORA11G
#include "utils/otlv4.h"
#include "utils/ipconnector/ipconnector.h"
#include "utils/coacommon.h"

extern otl_connect g_coMainDBConn;
extern char g_mcDebugFooter[256];

#define FOOT(a)	&(g_mcDebugFooter[sizeof(g_mcDebugFooter)-((a)>=sizeof(g_mcDebugFooter) ? sizeof(g_mcDebugFooter) - 1 : (a+1))])

/* ������ ��� ����������� ����� � ������� � ������ ������� */
#define	ENTER_ROUT	if (1 <= g_soMainCfg.m_iDebug) { \
		g_coLog.WriteLog ("%sdebugL1: %s started", FOOT(g_soMainCfg.m_iDCD), __func__); \
		++g_soMainCfg.m_iDCD; \
	}

/* ������ ��� ����������� ������ �� ������� � ������ ������� */
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
	otl_connect *m_pcoDBConn;					/* ��������� ������� ������ ��� �������������� � �� */
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

/* �������������� �������� � �� */
/* ������� ��������������� � ��.
   � ����� ��������� ��������������� ���������� 0
   � ������� ������� -200001 */
int ReconnectDB (otl_connect &p_coDBConn);
/* �������� ����������� � ��, � ������ ���� ������������ �������������� ���������� '0' */
int CheckDBConnection (otl_connect &p_coDBConn);
/* ���������� �������� ����������� � �� */
void DisconnectDB (otl_connect &p_coDBConn);
