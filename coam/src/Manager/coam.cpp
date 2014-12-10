#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <vector>
#include <string>
#include <map>
#include <pwd.h>
#include <grp.h>
#ifndef WIN32
#	include <strings.h>
#	define	stricmp	strcasecmp
#endif

#include "../../../utils/CoACommon.h"
#include "../../../utils/config/Config.h"
#include "../../../utils/log/Log.h"
#include "../../../utils/pspacket/PSPacket.h"
#include "../../../utils/timemeasurer/TimeMeasurer.h"
#include "coam.h"

/* глобальные переменные */
CConfig g_coConf;
CLog g_coLog;
SMainConfig g_soMainCfg;

static std::map<std::string,CConfig*> g_mapLocationConf;
static std::map<std::string,std::string> g_mapNASList;
static unsigned int g_uiReqNum = 0;

int LoadConf (const char *p_pszConfDir) {
	int iRetVal = 0;
	std::string strConfDir;
	std::string strConfFile;

	ENTER_ROUT;

	do {
		strConfDir = p_pszConfDir;
		if ('/' != strConfDir[strConfDir.length() - 1]) {
			strConfDir += '/';
		}
		strConfFile = strConfDir + "coam.conf";
		iRetVal = g_coConf.LoadConf (strConfFile.c_str());
		if (iRetVal) { iRetVal = -1; break; }

		std::vector<std::string> vectValList;
		std::vector<std::string>::iterator iterValList;
		CConfig *pcoTmp;

		g_coConf.GetParamValue ("location", vectValList);
		for (iterValList = vectValList.begin(); iterValList != vectValList.end(); ++iterValList) {
			strConfFile = strConfDir + *iterValList;
			pcoTmp = new CConfig;
			iRetVal = pcoTmp->LoadConf (strConfFile.c_str());
			if (iRetVal) { iRetVal = -1; break; }
			g_mapLocationConf.insert (std::make_pair (std::string(*iterValList), pcoTmp));
		}
		if (iRetVal) { break; }
	} while (0);

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

int InitCoAManager () {
	int iRetVal = 0;
	CTimeMeasurer coTM;
	char mcTimeDiff[32];

	ENTER_ROUT;

	do {
		std::vector<std::string> vectConfParam;
		std::vector<std::string>::iterator iterConfParam;

		iRetVal = g_coConf.GetParamValue ("log_file_mask", vectConfParam);
		if (iRetVal) {
			g_coLog.WriteLog ("InitCoAManager: error: Log file mask not defined");
			break;
		}
		iterConfParam = vectConfParam.begin();
		/* инициализация логгера */
		iRetVal = g_coLog.Init (iterConfParam->c_str());
		if (iRetVal) {
			g_coLog.WriteLog ("InitCoAManager: error: Can not initialize log writer: code: '%d'", iRetVal);
			break;
		}
		/* изменение пользователя и группы владельца демона */
		ChangeOSUser ();
		/* подключение к БД */
		coTM.Set ();
		iRetVal = ConnectDB (g_coMainDBConn);
		if (iRetVal) {
			g_coLog.WriteLog ("InitCoAManager: error: ConnectDB: code: '%d'", iRetVal);
			break;
		}
		coTM.GetDifference (NULL, mcTimeDiff, sizeof(mcTimeDiff));
		g_coLog.WriteLog ("InitCoAManager: info: DB connected successfully in '%s'", mcTimeDiff);
		/* создание списка NASов */
		coTM.Set ();
		iRetVal = CreateNASList (&g_mapNASList);
		if (iRetVal) {
			g_coLog.WriteLog ("InitCoAManager: error: CreateNASList: code: '%d'", iRetVal);
			break;
		}
		coTM.GetDifference (NULL, mcTimeDiff, sizeof(mcTimeDiff));
		g_coLog.WriteLog ("InitCoAManager: info: NAS list created successfully in '%s'", mcTimeDiff);
		/* инициализация пула потоков */
		iRetVal = InitThreadPool ();
		if (iRetVal) {
			break;
		}
	} while (0);

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

int DeInitCoAManager () {
	int iRetVal = 0;

	ENTER_ROUT;

	DisconnectDB (g_coMainDBConn);

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

void ChangeOSUser () {
	size_t stStrLen;
	char mcCmd[256];
	char mcCmdRes[256];
	FILE *psoFile;
	int iFnRes;
	const char *pszUser, *pszGroup;
	passwd *psoPswd;
	group *psoGroup;
	gid_t idUser, idGroup;
	std::string strVal;

	ENTER_ROUT;

	// изменяем id пользователя ОС
	iFnRes = g_coConf.GetParamValue ("os_user", strVal);
	if (0 == iFnRes) {
		pszUser = strVal.c_str();
		psoPswd = getpwnam (pszUser);
		if (psoPswd) {
			idUser = psoPswd->pw_uid;
		} else {
			idUser = (gid_t)-1;
		}
	}

	strVal.clear();
	iFnRes = g_coConf.GetParamValue ("os_group", strVal);
	if (0 == iFnRes) {
		pszGroup = strVal.c_str();
		psoGroup = getgrnam (pszGroup);
		if (psoGroup) {
			idGroup = psoGroup->gr_gid;
		} else {
			idGroup = (gid_t)-1;
		}
	}
	g_coLog.SetUGIds (idUser, idGroup);
	if ((gid_t)-1 != idUser) {
		setuid (idUser);
	}
	if ((gid_t)-1 != idGroup) {
		setgid (idGroup);
	}

	LEAVE_ROUT (0);
}

int ConnectCoASensor (CIPConnector &p_coIPConn) {
	int iRetVal = 0;
	std::vector<std::string> vectValList;
	const char *pcszConfParam;	// значение параметра из конфигурации
	const char *pcszHostName;		// имя удаленного хоста
	uint16_t ui16Port;					// порт удаленного хоста
	int iProtoType;							// тип протокола взаимодействия с удаленным хостом
	int iFnRes;

	ENTER_ROUT;

	do {
		// выбираем сведения о сетевом протоколе
		pcszConfParam = "coa_sensor_proto";
		iRetVal = g_coConf.GetParamValue (pcszConfParam, vectValList);
		if (iRetVal) {
			iRetVal = -1;
			g_coLog.WriteLog(
				"ConnectCoASensor: configuration parameter '%s' not found",
				pcszConfParam);
			break;
		}
		if (0 == stricmp ("TCP", vectValList[0].c_str())) {
			iProtoType = IPPROTO_TCP;
		}
		else if (0 == stricmp ("UDP", vectValList[0].c_str())) {
			iProtoType = IPPROTO_UDP;
		}
		else {
			iRetVal = -1;
			g_coLog.WriteLog(
				"ConnectCoASensor: configuration parameter '%s' containts unsuppurted value '%s'",
				pcszConfParam,
				vectValList[0].c_str());
			break;
		}
		// выбираем имя хоста CoA-сенсора
		vectValList.clear();
		pcszConfParam = "coa_sensor_addr";
		iRetVal = g_coConf.GetParamValue (pcszConfParam, vectValList);
		if (iRetVal) {
			g_coLog.WriteLog(
				"ConnectCoASensor: configuration parameter '%s' not found",
				pcszConfParam);
			break;
		}
		pcszHostName = vectValList[0].c_str();
		// выбираем порт CoASensor-а
		vectValList.clear();
		pcszConfParam = "coa_sensor_port";
		iRetVal = g_coConf.GetParamValue (pcszConfParam, vectValList);
		if (iRetVal) {
			g_coLog.WriteLog(
				"ConnectCoASensor: configuration parameter '%s' not found",
				pcszConfParam);
			break;
		}
		ui16Port = atol (vectValList[0].c_str());

		/* подключаемся к удаленному хосту */
		iFnRes = p_coIPConn.Connect (pcszHostName, ui16Port, iProtoType);
		if (iFnRes) {
			iRetVal = -1;
			g_coLog.WriteLog ("ConnectCoASensor: can't connect to CoASensor");
			break;
		}
	} while (0);

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

int RequestTimer () {
	int iRetVal = 0;

	MainWork ();

	return iRetVal;
}

int MainWork ()
{
	int iRetVal = 0;
	std::vector<SSubscriberRefresh> vectSubscriberList;
	std::vector<SSubscriberRefresh>::iterator iterSubscr;

	ENTER_ROUT;

	do {
		/* загружаем список абонентов */
		iRetVal = CreateSubscriberList (&vectSubscriberList);
		if (iRetVal) {
			g_coLog.WriteLog ("CreateSubscriberList failed");
			break;
		}
		/* обходим список абонентов */
		iterSubscr = vectSubscriberList.begin();
		while (iterSubscr != vectSubscriberList.end()) {
			/* обрабатыаем учетную запись абонента */
			iRetVal = ThreadManager (*iterSubscr);
			if (iRetVal) {
				break;
			}
			++iterSubscr;
		}
	} while (0);

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

bool operator < (const SSessionInfo &soLeft, const SSessionInfo &soRight) {
	bool bRetVal;
	int iFnRes;

	iFnRes = strcmp (soLeft.m_mcUserName, soRight.m_mcUserName);
	if (0 > iFnRes) {
		return true;
	}
	if (0 != iFnRes) {
		return false;
	}

	iFnRes = strcmp (soLeft.m_mcNasIPAddress, soRight.m_mcNasIPAddress);
	if (0 > iFnRes) {
		return true;
	}
	if (0 != iFnRes) {
		return false;
	}

	iFnRes = strcmp (soLeft.m_mcCiscoParentSessionId, soRight.m_mcCiscoParentSessionId);
	if (0 > iFnRes) {
		return true;
	}
	if (0 != iFnRes) {
		return false;
	}

	iFnRes = strcmp (soLeft.m_mcLocation, soRight.m_mcLocation);
	if (0 > iFnRes) {
		return true;
	}

	return false;
}

bool operator < (const SPolicyInfo &soLeft, const SPolicyInfo &soRight) {
	bool bRetVal;
	int iFnRes;

	iFnRes = strcmp (soLeft.m_mcUserName, soRight.m_mcUserName);
	if (0 > iFnRes) {
		return true;
	}
	if (0 != iFnRes) {
		return false;
	}

	iFnRes = strcmp (soLeft.m_mcLocation, soRight.m_mcLocation);
	if (0 > iFnRes) {
		return true;
	}

	return false;
}

bool operator < (const SPolicyDetail &soLeft, const SPolicyDetail &soRight) {
	bool bRetVal;
	int iFnRes;

	iFnRes = strcmp (soLeft.m_mcAttr, soRight.m_mcAttr);
	if (0 > iFnRes) {
		return true;
	}
	if (0 != iFnRes) {
		return false;
	}

	iFnRes = strcmp (soLeft.m_mcValue, soRight.m_mcValue);
	if (0 > iFnRes) {
		return true;
	}

	return false;
}

int OperateSubscriber (const SSubscriberRefresh &p_soRefreshRecord, CIPConnector *p_pcoIPConn, otl_connect &p_coDBConn)
{
	int iRetVal = 0;
	CTimeMeasurer coTM;
	std::map<SSessionInfo,std::map<std::string,int> > mapSessionList; /* список сессий подписчика */
	std::map<SSessionInfo,std::map<std::string,int> >::iterator iterSession; /* итератор списка сессий подписчика */
	int stPolicyCnt;
	bool bPolicyFound;

	ENTER_ROUT;

	g_coLog.WriteLog ("OperateSubscriber: Subscriber_id: '%s';", p_soRefreshRecord.m_mcSubscriberId);

	do {
		/* загружаем список сессий подписчика */
		iRetVal = CreateSessionList (p_soRefreshRecord.m_mcSubscriberId, &mapSessionList, p_coDBConn);
		if (iRetVal) {
			/* если не удалось загрузить список сессий подписчика */
			g_coLog.WriteLog ("OperateSubscriber: error: CreateSessionList: code: '%d'", iRetVal);
			break;
		}

		iterSession = mapSessionList.begin();

		std::map<SPolicyInfo,std::map<SPolicyDetail,int> > mapPolicyList;

		// обходим все сессии абонента
		while (iterSession != mapSessionList.end()) {
			iRetVal = OperateSubscriberSession (p_soRefreshRecord, iterSession->first, iterSession->second, mapPolicyList, *p_pcoIPConn, p_coDBConn);
			if (iRetVal) {
				break;
			}
			++iterSession;
		}
	} while (0);

	char mcTimeDiff[32];
	coTM.GetDifference (NULL, mcTimeDiff, sizeof(mcTimeDiff));
	g_coLog.WriteLog ("OperateSubscriber: Subscriber_id: '%s' operated in '%s'", p_soRefreshRecord.m_mcSubscriberId, mcTimeDiff);
	
	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

int OperateSubscriberSession (
	const SSubscriberRefresh &p_soRefreshRecord,
	const SSessionInfo &p_soSessionInfo,
	std::map<std::string,int> &p_soSessionPolicyList,
	std::map<SPolicyInfo,std::map<SPolicyDetail,int> > &p_mapProfilePolicyList,
	CIPConnector &p_coIPConn,
	otl_connect &p_coDBConn)
{
	int iRetVal = 0;

	ENTER_ROUT;

	do {
		SPolicyInfo soPolicyInfo;
		std::map<SPolicyInfo,std::map<SPolicyDetail,int> >::iterator iterProfilePolicy = p_mapProfilePolicyList.end ();
		std::map<SPolicyInfo,std::map<SPolicyDetail,int> >::iterator iterProfilePolicyDef = p_mapProfilePolicyList.end ();

		g_coLog.WriteLog(
			"OperateSubscriberSession: Subscriber_id: '%s'; UserName: '%s'; NASIPAddress: '%s'; SessionID: '%s'; Location: '%s';",
			p_soRefreshRecord.m_mcSubscriberId,
			p_soSessionInfo.m_mcUserName,
			p_soSessionInfo.m_mcNasIPAddress,
			p_soSessionInfo.m_mcCiscoParentSessionId,
			p_soSessionInfo.m_mcLocation);
		/* ищем кешированные политики */
		strcpy (soPolicyInfo.m_mcUserName, p_soSessionInfo.m_mcUserName);
		// ищем кешированную политику для локации сервера доступа
		strcpy (soPolicyInfo.m_mcLocation, p_soSessionInfo.m_mcLocation);
		iterProfilePolicy = p_mapProfilePolicyList.find (soPolicyInfo);
		// ищем кешированную политику для локации DEFAULT
		if (strcmp (p_soSessionInfo.m_mcLocation, "DEFAULT")) {
			strcpy (soPolicyInfo.m_mcLocation, "DEFAULT");
			iterProfilePolicyDef = p_mapProfilePolicyList.find (soPolicyInfo);
		}
		/* если кешированные политики не найдены запрашиваем их из БД */
		if (iterProfilePolicy == p_mapProfilePolicyList.end() && iterProfilePolicyDef == p_mapProfilePolicyList.end()) {
			iRetVal = CreatePolicyList (&(p_soSessionInfo), &p_mapProfilePolicyList, p_coDBConn);
			/* если произошла ошибка при формировании списка политик завершаем работу */
			if (iRetVal) {
				g_coLog.WriteLog (
					"OperateSubscriberSession: Subscriber_id: '%s': error: CreatePolicyList: code: '%d'",
					p_soRefreshRecord.m_mcSubscriberId,
					iRetVal);
				break;
			}
			/* повторно ищем кешированную политику для локации сервера доступа */
			strcpy (soPolicyInfo.m_mcLocation, p_soSessionInfo.m_mcLocation);
			iterProfilePolicy = p_mapProfilePolicyList.find (soPolicyInfo);
			/* повторно ищем кешированную политику для локации DEFAULT */
			if (strcmp (p_soSessionInfo.m_mcLocation, "DEFAULT")) {
				strcpy (soPolicyInfo.m_mcLocation, "DEFAULT");
				iterProfilePolicyDef = p_mapProfilePolicyList.find (soPolicyInfo);
			}
		}
		/* если в записи очереди команд задано значение 'action' */
		if (p_soRefreshRecord.m_mcAction[0]) {
			/* отрабатываем команду 'logoff' */
			if (0 == strcmp ("logoff", p_soRefreshRecord.m_mcAction)) {
				iRetVal = AccountLogoff (&(p_soSessionInfo), &p_coIPConn);
				if (iRetVal) {
					if (-45 == iRetVal) {
						iRetVal = 0;
					} else {
						g_coLog.WriteLog (
							"OperateSubscriberSession: Subscriber_id: '%s'; UserName: '%s'; NASIPAddress: '%s'; SessionID: '%s': error occurred while 'AccountLogoff' processing",
							p_soRefreshRecord.m_mcSubscriberId,
							p_soSessionInfo.m_mcUserName,
							p_soSessionInfo.m_mcNasIPAddress,
							p_soSessionInfo.m_mcCiscoParentSessionId);
						break;
					}
				}
				g_coLog.WriteLog ("OperateSubscriberSession: Subscriber_id: '%s'; UserName: '%s'; NASIPAddress: '%s'; SessionID: '%s'; action: '%s'; User is Disconnected",
					p_soRefreshRecord.m_mcSubscriberId,
					p_soSessionInfo.m_mcUserName,
					p_soSessionInfo.m_mcNasIPAddress,
					p_soSessionInfo.m_mcCiscoParentSessionId,
					p_soRefreshRecord.m_mcAction);
			}
			/* отрабатываем команду 'checksession' */
			else if (0 == strcmp ("checksession", p_soRefreshRecord.m_mcAction)) {
				iRetVal = CheckSession (&(p_soSessionInfo), &p_coIPConn, p_coDBConn);
				switch (iRetVal) {
				case -45:
					iRetVal = 0;
					g_coLog.WriteLog ("OperateSubscriberSession: Subscriber_id: '%s'; UserName: '%s'; NASIPAddress: '%s'; SessionID: '%s'; action: '%s'; user session fixed",
						p_soRefreshRecord.m_mcSubscriberId,
						p_soSessionInfo.m_mcUserName,
						p_soSessionInfo.m_mcNasIPAddress,
						p_soSessionInfo.m_mcCiscoParentSessionId,
						p_soRefreshRecord.m_mcAction);
					break;
				case 0:
					g_coLog.WriteLog ("OperateSubscriberSession: Subscriber_id: '%s'; UserName: '%s'; NASIPAddress: '%s'; SessionID: '%s'; action: '%s'; user session is active - nothing to do",
						p_soRefreshRecord.m_mcSubscriberId,
						p_soSessionInfo.m_mcUserName,
						p_soSessionInfo.m_mcNasIPAddress,
						p_soSessionInfo.m_mcCiscoParentSessionId,
						p_soRefreshRecord.m_mcAction);
					break;
				default:
					g_coLog.WriteLog (
						"OperateSubscriberSession: Subscriber_id: '%s'; UserName: '%s'; NASIPAddress: '%s'; SessionID: '%s': error occurred while 'CheckSession' processing",
						p_soRefreshRecord.m_mcSubscriberId,
						p_soSessionInfo.m_mcUserName,
						p_soSessionInfo.m_mcNasIPAddress,
						p_soSessionInfo.m_mcCiscoParentSessionId);
					break;
				}
			}
			/* неизвестное значение 'action' */
			else {
				g_coLog.WriteLog ("OperateSubscriberSession: Subscriber_id: '%s': error: action: '%s' is not supported",
					p_soRefreshRecord.m_mcSubscriberId,
					p_soRefreshRecord.m_mcAction);
				iRetVal = -1;
			}
		}
		/* если политики не найдены */
		else if (iterProfilePolicy == p_mapProfilePolicyList.end() && iterProfilePolicyDef == p_mapProfilePolicyList.end()) {
			// политики не найдены, посылаем LogOff
			iRetVal = AccountLogoff (&(p_soSessionInfo), &p_coIPConn);
			if (iRetVal) {
				if (-45 == iRetVal) {
					iRetVal = 0;
				} else {
					g_coLog.WriteLog (
						"OperateSubscriberSession: Subscriber_id: '%s'; UserName: '%s'; NASIPAddress: '%s'; SessionID: '%s': error occurred while 'AccountLogoff' processing",
							p_soRefreshRecord.m_mcSubscriberId,
							p_soSessionInfo.m_mcUserName,
							p_soSessionInfo.m_mcNasIPAddress,
							p_soSessionInfo.m_mcCiscoParentSessionId);
					break;
				}
			}
			g_coLog.WriteLog ("OperateSubscriberSession: Subscriber_id: '%s'; UserName: '%s'; NASIPAddress: '%s'; SessionID: '%s'; Actual policies not found. User is Disconnected",
				p_soRefreshRecord.m_mcSubscriberId,
				p_soSessionInfo.m_mcUserName,
				p_soSessionInfo.m_mcNasIPAddress,
				p_soSessionInfo.m_mcCiscoParentSessionId);
		}
		/* обрабатываем политики */
		else {
			if (iterProfilePolicy != p_mapProfilePolicyList.end ()) {
				SelectActualPolicy (&p_soSessionPolicyList, &(iterProfilePolicy->second));
			}
			if (iterProfilePolicyDef != p_mapProfilePolicyList.end ()) {
				SelectActualPolicy (&p_soSessionPolicyList, &(iterProfilePolicyDef->second));
			}
			/* отключаем активные неактуальные политики */
			iRetVal = DeactivateNotrelevantPolicy (p_soRefreshRecord, p_soSessionInfo, p_soSessionPolicyList, p_coIPConn);
			if (iRetVal) {
				break;
			}
			/* включаем неактивные актуальные политики */
			if (iterProfilePolicy != p_mapProfilePolicyList.end()) {
				iRetVal = ActivateInactivePolicy (p_soRefreshRecord, p_soSessionInfo, iterProfilePolicy->second, p_coIPConn);
				if (iRetVal) {
					break;
				}
			}
			if (iterProfilePolicyDef != p_mapProfilePolicyList.end()) {
				iRetVal = ActivateInactivePolicy (p_soRefreshRecord, p_soSessionInfo, iterProfilePolicyDef->second, p_coIPConn);
				if (iRetVal) {
					break;
				}
			}
		}
		if (iRetVal) {
			break;
		}
	} while (0);

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

int DeactivateNotrelevantPolicy (const SSubscriberRefresh &p_soRefreshRecord, const SSessionInfo &p_soSessionInfo, std::map<std::string,int> &p_soSessionPolicyList, CIPConnector &p_coIPConn)
{
	int iRetVal = 0;

	ENTER_ROUT;

	do {
		std::map<std::string,int>::iterator iterSessionPolicy;
		iterSessionPolicy = p_soSessionPolicyList.begin ();
		const char *mcstrSessState[] = {"not actual", "actual"};
		while (iterSessionPolicy != p_soSessionPolicyList.end ()) {
			if (2 <= g_soMainCfg.m_iDebug) {
				g_coLog.WriteLog ("%sService: '%s'; State: '%s'", FOOT(g_soMainCfg.m_iDCD), iterSessionPolicy->first.c_str(), mcstrSessState[iterSessionPolicy->second]);
			}
			if (0 == iterSessionPolicy->second) {
				iRetVal = DeActivateService (&p_soSessionInfo, iterSessionPolicy->first.c_str(), NULL, &p_coIPConn);
				if (iRetVal) {
					if (-45 == iRetVal) {
						iRetVal = 0;
					} else {
						g_coLog.WriteLog (
							"DeactivateNotrelevantPolicy: Subscriber_id: '%s'; UserName: '%s'; NASIPAddress: '%s'; SessionID: '%s': error occurred while 'DeActivateService' processing",
								p_soRefreshRecord.m_mcSubscriberId,
								p_soSessionInfo.m_mcUserName,
								p_soSessionInfo.m_mcNasIPAddress,
								p_soSessionInfo.m_mcCiscoParentSessionId);
						break;
					}
				}
				g_coLog.WriteLog ("DeactivateNotrelevantPolicy: Subscriber_id: '%s'; UserName: '%s'; NASIPAddress: '%s'; SessionID: '%s'; Policy '%s' deactivated",
					p_soRefreshRecord.m_mcSubscriberId,
					p_soSessionInfo.m_mcUserName,
					p_soSessionInfo.m_mcNasIPAddress,
					p_soSessionInfo.m_mcCiscoParentSessionId,
					iterSessionPolicy->first.c_str());
			}
			++iterSessionPolicy;
		}
	} while (0);

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

int ActivateInactivePolicy (
	const SSubscriberRefresh &p_soRefreshRecord,
	const SSessionInfo &p_soSessionInfo,
	std::map<SPolicyDetail,int> &p_mapPolicyDetail,
	CIPConnector &p_coIPConn)
{
	int iRetVal = 0;

	ENTER_ROUT;

	do {
		const char *mcstrPolicyState[] = {"inactive", "active"};
		std::map<SPolicyDetail,int>::iterator iterProfilePolicyDetail;
		iterProfilePolicyDetail = p_mapPolicyDetail.begin();
		while (iterProfilePolicyDetail != p_mapPolicyDetail.end()) {
			if (2 <= g_soMainCfg.m_iDebug) {
				g_coLog.WriteLog ("%sPolicy: '%s'; State: '%s'", FOOT(g_soMainCfg.m_iDCD), iterProfilePolicyDetail->first.m_mcValue, mcstrPolicyState[iterProfilePolicyDetail->second]);
			}
			if (0 == iterProfilePolicyDetail->second) {
				iRetVal = ActivateService (&p_soSessionInfo, iterProfilePolicyDetail->first.m_mcValue, iterProfilePolicyDetail->first.m_mcAttr, &p_coIPConn);
				if (iRetVal) {
					if (-45 == iRetVal) {
						iRetVal = 0;
					} else {
						g_coLog.WriteLog (
							"ActivateInactivePolicy: Subscriber_id: '%s'; UserName: '%s'; NASIPAddress: '%s'; SessionID: '%s'; Policy '%s': error occurred while 'ActivateService' processing",
							p_soRefreshRecord.m_mcSubscriberId,
							p_soSessionInfo.m_mcUserName,
							p_soSessionInfo.m_mcNasIPAddress,
							p_soSessionInfo.m_mcCiscoParentSessionId,
							iterProfilePolicyDetail->first.m_mcValue);
						break;
					}
				}
				g_coLog.WriteLog ("ActivateInactivePolicy: Subscriber_id: '%s'; UserName: '%s'; NASIPAddress: '%s'; SessionID: '%s'; Policy '%s' activated",
					p_soRefreshRecord.m_mcSubscriberId,
					p_soSessionInfo.m_mcUserName,
					p_soSessionInfo.m_mcNasIPAddress,
					p_soSessionInfo.m_mcCiscoParentSessionId,
					iterProfilePolicyDetail->first.m_mcValue);
			}
			++iterProfilePolicyDetail;
		}
	} while (0);

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

int GetNASLocation (char *p_pszNASName, char *p_pszLocation, int p_iBufSize)
{
	int iRetVal = 0;
	std::map<std::string,std::string>::iterator iterNASList;
	size_t stCount;

	ENTER_ROUT;

	iterNASList = g_mapNASList.find (p_pszNASName);
	if (iterNASList != g_mapNASList.end()) {
		stCount = iterNASList->second.length() > p_iBufSize ? p_iBufSize : iterNASList->second.length() + 1;
		strncpy(
			p_pszLocation,
			iterNASList->second.c_str(),
			stCount);
		if (2 <= g_soMainCfg.m_iDebug) {
			g_coLog.WriteLog ("%sNASName: '%s'; Location: '%s'", FOOT(g_soMainCfg.m_iDCD), p_pszNASName, p_pszLocation);
		}
	} else {
		if (1 <= g_soMainCfg.m_iDebug) {
			g_coLog.WriteLog ("%sLocation for '%s' not found", FOOT(g_soMainCfg.m_iDCD), p_pszNASName);
		}
		iRetVal = -1;
	}

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

int ModifyName (const char *p_pszModifyRule, char *p_pszLocation, char *p_pszValue)
{
	int iRetVal = 0;
	char mcLocation[128];
	std::map<std::string,CConfig*>::iterator iterLocation;
	std::vector<std::string> vectValList;
	std::vector<std::string>::iterator iterValList;
	CConfig *pcoLocConf;
	std::string strModifyName;

	ENTER_ROUT;

	if (2 <= g_soMainCfg.m_iDebug) {
		g_coLog.WriteLog ("%sOriginal name: '%s'; Location: '%s'", FOOT(g_soMainCfg.m_iDCD), p_pszValue, p_pszLocation);
	}

	do {
		strModifyName = p_pszValue;
		iterLocation = g_mapLocationConf.find (p_pszLocation);
		if (iterLocation == g_mapLocationConf.end()) {
			g_coLog.WriteLog ("ModifyName: Location '%s' configuration not found", p_pszLocation);
			iRetVal = -1;
			break;
		}
		pcoLocConf = iterLocation->second;
		if (NULL == pcoLocConf) {
			g_coLog.WriteLog ("ModifyName: Location '%s' configuration not exists", p_pszLocation);
			iRetVal = -1;
			break;
		}
		// выбираем первое правило
		iRetVal = pcoLocConf->GetParamValue (p_pszModifyRule, vectValList);
		if (iRetVal) { break; }
		iterValList = vectValList.begin();
		// обходим все правила изменения имен сервисов
		while (iterValList != vectValList.end()) {
			// если длина префикса меньше или равна имени сервиса
			if (2 <= g_soMainCfg.m_iDebug) {
				g_coLog.WriteLog ("%sCheck: '%s'", FOOT(g_soMainCfg.m_iDCD), iterValList->c_str());
			}
			if (iterValList->length() <= strModifyName.length()) {
				int iFnRes;
				// сравниваем префикс с началом имени сервиса
				iFnRes = memcmp(
					iterValList->c_str(),
					strModifyName.c_str(),
					iterValList->length());
				// если перфикс и начало имени сервиса совпадают
				if (0 == iFnRes) {
					if (2 <= g_soMainCfg.m_iDebug) {
						g_coLog.WriteLog ("%s'%s' applying", FOOT(g_soMainCfg.m_iDCD), iterValList->c_str());
					}
					int iPrfLen = iterValList->length();
					std::basic_string<char> bstrTmp;
					// убираем префикс
					bstrTmp = strModifyName.substr (iPrfLen, strModifyName.length() - iPrfLen);
					if (2 <= g_soMainCfg.m_iDebug) {
						g_coLog.WriteLog ("%sService: '%s'; Substring: '%s'", FOOT(g_soMainCfg.m_iDCD), strModifyName.c_str(), bstrTmp.c_str());
					}
					strcpy (p_pszValue, bstrTmp.c_str());
					// завершаем поиск правила для имени сервиса
					break;
				}
			}
			++iterValList;
		}
	} while (0);

	if (2 <= g_soMainCfg.m_iDebug) {
		g_coLog.WriteLog ("%sModified name: '%s'", FOOT(g_soMainCfg.m_iDCD), p_pszValue);
	}

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

bool Filter (const char *p_pszFilterName, char *p_pszLocation, char *p_pszValue)
{
	bool bRetVal = false;
	char mcLocation[128];
	std::map<std::string,CConfig*>::iterator iterLocation;
	std::vector<std::string> vectValList;
	std::vector<std::string>::iterator iterValList;
	CConfig *pcoLocConf;
	int iValueLen = strlen (p_pszValue);

	ENTER_ROUT;

	if (2 <= g_soMainCfg.m_iDebug) {
		g_coLog.WriteLog ("%sValue: '%s'; Location: '%s'", FOOT(g_soMainCfg.m_iDCD), p_pszValue, p_pszLocation);
	}

	do {
		// ищем конфигурацию локации
		iterLocation = g_mapLocationConf.find (p_pszLocation);
		if (iterLocation == g_mapLocationConf.end()) {
			g_coLog.WriteLog ("Filter: Location '%s' configuration not found", p_pszLocation);
			bRetVal = true;
			break;
		}
		pcoLocConf = iterLocation->second;
		if (NULL == pcoLocConf) {
			g_coLog.WriteLog ("Filter: Location '%s' configuration not exists", p_pszLocation);
			bRetVal = true;
			break;
		}
		/* выбираем фильтры для локации */
		if (0 != pcoLocConf->GetParamValue (p_pszFilterName, vectValList)) {
			if (2 <= g_soMainCfg.m_iDebug) {
				g_coLog.WriteLog ("%sLocation: '%s'; Value: '%s'; Filter name: '%s'. Filter not found.", FOOT(g_soMainCfg.m_iDCD), p_pszLocation, p_pszValue, p_pszFilterName);
			}
			bRetVal = true;
			break;
		}
		// обходим все правила изменения имен сервисов
		for (iterValList = vectValList.begin(); iterValList != vectValList.end(); ++iterValList) {
			if (2 <= g_soMainCfg.m_iDebug) {
				g_coLog.WriteLog ("%sCheck: '%s'", FOOT(g_soMainCfg.m_iDCD), iterValList->c_str());
			}
			// если длина значения больше или равна длине значения фильтра
			if (iValueLen >= iterValList->length()) {
				int iFnRes;
				// сравниваем префикс с началом имени сервиса
				iFnRes = memcmp(
					iterValList->c_str(),
					p_pszValue,
					iterValList->length());
				// если перфикс и начало имени сервиса совпадают
				if (0 == iFnRes) {
					if (2 <= g_soMainCfg.m_iDebug) {
						g_coLog.WriteLog ("%s'%s' applying", FOOT(g_soMainCfg.m_iDCD), iterValList->c_str());
					}
					bRetVal = true;
					break;
				}
			}
		}
	} while (0);

	LEAVE_ROUT (bRetVal);

	return bRetVal;
}

int SelectActualPolicy (std::map<std::string,int> *p_pmapSessionDetail, std::map<SPolicyDetail,int> *p_pmapPolicyDetail)
{
	int iRetVal = 0;
	std::map<std::string,int>::iterator iterSessionPolicy;
	std::map<SPolicyDetail,int>::iterator iterProfilePolicy;
	int iFnRes;

	ENTER_ROUT;

	iterSessionPolicy = p_pmapSessionDetail->begin();
	while (iterSessionPolicy != p_pmapSessionDetail->end()) {
		if (2 <= g_soMainCfg.m_iDebug) {
			g_coLog.WriteLog ("%sService name '%s'", FOOT(g_soMainCfg.m_iDCD), iterSessionPolicy->first.c_str());
		}
		iterProfilePolicy = p_pmapPolicyDetail->begin();
		while (iterProfilePolicy != p_pmapPolicyDetail->end()) {
			if (2 <= g_soMainCfg.m_iDebug) {
				g_coLog.WriteLog ("%sCheck '%s'", FOOT(g_soMainCfg.m_iDCD), iterProfilePolicy->first.m_mcValue);
			}
			iFnRes = iterSessionPolicy->first.compare (iterProfilePolicy->first.m_mcValue);
			if (0 == iFnRes) {
				if (2 <= g_soMainCfg.m_iDebug) {
					g_coLog.WriteLog ("%s'%s' applied", FOOT(g_soMainCfg.m_iDCD), iterProfilePolicy->first.m_mcValue);
				}
				iterSessionPolicy->second = 1;
				iterProfilePolicy->second = 1;
				break;
			}
			++iterProfilePolicy;
		}
		++iterSessionPolicy;
	}

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

int SetCommonCoASensorAttr (SPSRequest *p_psoRequest, size_t p_stBufSize, const SSessionInfo *p_pcsoSessionInfo, CConfig *p_pcoConf, CIPConnector *p_pcoIPConn)
{
	int iRetVal = 0;
	int iFnRes;
	CPSPacket coPSPack;
	__uint16_t ui16PackLen;
	__uint16_t ui16ValueLen;

	ENTER_ROUT;

	do {
		if (2 != p_pcoIPConn->GetStatus ()) {
			iFnRes = ConnectCoASensor (*p_pcoIPConn);
			if (iFnRes) {
				iRetVal = iFnRes;
				g_coLog.WriteLog ("SetCommonCoASensorAttr: can not connect to CoASensor");
				break;
			}
		}

		coPSPack.Init (p_psoRequest, p_stBufSize, g_uiReqNum++, COMMAND_REQ);

		// добавляем атрибут PS_NASIP
		ui16ValueLen = strlen (p_pcsoSessionInfo->m_mcNasIPAddress);
		ui16PackLen = coPSPack.AddAttr (p_psoRequest, p_stBufSize, PS_NASIP, p_pcsoSessionInfo->m_mcNasIPAddress, ui16ValueLen, 0);

		// добавляем атрибут PS_SESSID
		ui16ValueLen = strlen (p_pcsoSessionInfo->m_mcCiscoParentSessionId);
		ui16PackLen = coPSPack.AddAttr (p_psoRequest, p_stBufSize, PS_SESSID, p_pcsoSessionInfo->m_mcCiscoParentSessionId, ui16ValueLen, 0);

		// дополнительные параметры запроса
		std::vector<std::string> vectParamVal;
		std::vector<std::string>::iterator iterValList;
		char mcAdditAttr[0x1000], *pszVal;
		__uint16_t ui16AttrType;
		p_pcoConf->GetParamValue ("additional_req_attr", vectParamVal);
		for (iterValList = vectParamVal.begin(); iterValList != vectParamVal.end(); ++iterValList) {
			strncpy (mcAdditAttr, iterValList->c_str(), sizeof(mcAdditAttr));
			pszVal = strstr (mcAdditAttr, "=");
			if (NULL == pszVal) {
				continue;
			}
			*pszVal = '\0';
			++pszVal;
			ui16AttrType = strtol (mcAdditAttr, NULL, 0);
			ui16ValueLen = strlen (pszVal);
			ui16PackLen = coPSPack.AddAttr (p_psoRequest, p_stBufSize, ui16AttrType, pszVal, ui16ValueLen, 0);
		}

	} while (0);

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

int DeActivateService (
	const SSessionInfo *p_pcsoSessInfo,
	const char *p_pcszServiceInfo,
	const char *p_pcszAttr,
	CIPConnector *p_pcoIPConn)
{
	int iRetVal = 0;
	char mcPack[0x10000];
	__uint16_t ui16PackLen;
	__uint16_t ui16ValueLen;
	CPSPacket coPSPack;
	SPSRequest *psoReq;

	ENTER_ROUT;

	do {
		psoReq = (SPSRequest*)mcPack;

		// ищем конфигурацию локации
		std::map<std::string,CConfig*>::iterator iterLocConf;
		CConfig *pcoLocConf;
		iterLocConf = g_mapLocationConf.find (p_pcsoSessInfo->m_mcLocation);
		// если конфигурация локации не найдена
		if (iterLocConf == g_mapLocationConf.end()) {
			g_coLog.WriteLog ("DeActivateService: Location '%s' configuration not found", p_pcsoSessInfo->m_mcLocation);
			iRetVal = -1;
			break;
		}
		pcoLocConf = iterLocConf->second;

		iRetVal = SetCommonCoASensorAttr (psoReq, sizeof (mcPack), p_pcsoSessInfo, pcoLocConf, p_pcoIPConn);
		if (iRetVal) {
			g_coLog.WriteLog ("DeActivateService: SetCommonCoASensorAttr: error: code: '%d'", iRetVal);
			break;
		}

		// добавляем атрибут PS_COMMAND
		const char *pcszConfParamName;
		char mcCommand[0x400];
		std::string strCmd;
		std::string strUseAttrName;

		pcszConfParamName = "deactivation";
		// запрашиваем значение конфигурационного параметра
		iRetVal = pcoLocConf->GetParamValue (pcszConfParamName, strCmd);
		// если параметр не найден
		if (iRetVal) {
			g_coLog.WriteLog ("DeActivateService: Config parameter '%s' not found", pcszConfParamName);
			break;
		}
		// или его значение не задано
		if (0 == strCmd.length()) {
			g_coLog.WriteLog ("DeActivateService: Config parameter '%s' not defined", pcszConfParamName);
			iRetVal -1;
			break;
		}
		// запрашиваем значение конфигурационного параметра
		pcszConfParamName = "use_policy_attr_name";
		iRetVal = pcoLocConf->GetParamValue (pcszConfParamName, strUseAttrName);
		// если параметр не найден
		if (iRetVal) {
			g_coLog.WriteLog ("DeActivateService: Config parameter '%s' not found", pcszConfParamName);
			break;
		}
		if (0 == strUseAttrName.compare ("yes") && NULL != p_pcszAttr) {
			ui16ValueLen = snprintf (mcCommand, sizeof(mcCommand), "%s=%s=%s", strCmd.c_str(), p_pcszAttr, p_pcszServiceInfo);
		} else {
			ui16ValueLen = snprintf (mcCommand, sizeof(mcCommand), "%s=%s", strCmd.c_str(), p_pcszServiceInfo);
		}
		ui16PackLen = coPSPack.AddAttr (psoReq, sizeof(mcPack), PS_COMMAND, mcCommand, ui16ValueLen, 0);

		iRetVal = p_pcoIPConn->Send (mcPack, ui16PackLen);
		if (iRetVal) {
			g_coLog.WriteLog ("DeActivateService: error occurred while processing 'p_pcoIPConn->Send'. error code: '%d'", iRetVal);
			break;
		}
		if (2 <= g_soMainCfg.m_iDebug) {
			ParsePSPack ((SPSRequest*)mcPack, ui16PackLen, 0);
		}

		iRetVal = p_pcoIPConn->Recv (mcPack, sizeof(mcPack));
		if (0 == iRetVal) {
			g_coLog.WriteLog ("DeActivateService: connection is closed");
			iRetVal = -1;
			break;
		}
		if (0 > iRetVal) {
			g_coLog.WriteLog ("DeActivateService: error occurred while processing 'p_pcoIPConn->Recv'. error code: '%d'", iRetVal);
			break;
		}
		iRetVal = ParsePSPack ((SPSRequest*)mcPack, iRetVal);
	} while (0);

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

int ActivateService (
	const SSessionInfo *p_pcsoSessInfo,
	const char *p_pcszServiceInfo,
	const char *p_pcszAttr,
	CIPConnector *p_pcoIPConn)
{
	int iRetVal = 0;
	char mcPack[0x10000];
	__uint16_t ui16PackLen;
	__uint16_t ui16ValueLen;
	CPSPacket coPSPack;
	SPSRequest *psoReq;

	ENTER_ROUT;

	do {
		psoReq = (SPSRequest*)mcPack;

		// ищем конфигурацию локации
		std::map<std::string,CConfig*>::iterator iterLocConf;
		CConfig *pcoLocConf;
		iterLocConf = g_mapLocationConf.find (p_pcsoSessInfo->m_mcLocation);
		// если конфигурация локации не найдена
		if (iterLocConf == g_mapLocationConf.end()) {
			g_coLog.WriteLog ("ActivateService: Location '%s' configuration not found", p_pcsoSessInfo->m_mcLocation);
			iRetVal = -1;
			break;
		}
		pcoLocConf = iterLocConf->second;

		iRetVal = SetCommonCoASensorAttr (psoReq, sizeof (mcPack), p_pcsoSessInfo, pcoLocConf, p_pcoIPConn);
		if (iRetVal) {
			g_coLog.WriteLog ("ActivateService: SetCommonCoASensorAttr: error: code: '%d'", iRetVal);
			break;
		}

		// добавляем атрибут PS_COMMAND
		const char *pcszConfParamName;
		char mcCommand[0x400];
		std::string strCmd;
		std::string strUserAttrName;

		pcszConfParamName = "activation";
		// запрашиваем значение конфигурационного параметра
		iRetVal = pcoLocConf->GetParamValue (pcszConfParamName, strCmd);
		// если параметр не найден
		if (iRetVal) {
			g_coLog.WriteLog ("ActivateService: Config parameter '%s' not found", pcszConfParamName);
			break;
		}
		// или его значение не задано
		if (0 == strCmd.length()) {
			g_coLog.WriteLog ("ActivateService: Config parameter '%s' not defined", pcszConfParamName);
			iRetVal -1;
			break;
		}
		// запрашиваем значение конфигурационного параметра
		pcszConfParamName = "use_policy_attr_name";
		iRetVal = pcoLocConf->GetParamValue (pcszConfParamName, strUserAttrName);
		// если параметр не найден
		if (iRetVal) {
			g_coLog.WriteLog ("ActivateService: Config parameter '%s' not found", pcszConfParamName);
			break;
		}
		if (0 == strUserAttrName.compare ("yes") && NULL != p_pcszAttr) {
			ui16ValueLen = snprintf (mcCommand, sizeof(mcCommand), "%s=%s=%s", strCmd.c_str(), p_pcszAttr, p_pcszServiceInfo);
		} else {
			ui16ValueLen = snprintf (mcCommand, sizeof(mcCommand), "%s=%s", strCmd.c_str(), p_pcszServiceInfo);
		}
		ui16PackLen = coPSPack.AddAttr (psoReq, sizeof(mcPack), PS_COMMAND, mcCommand, ui16ValueLen, 0);

		iRetVal = p_pcoIPConn->Send (mcPack, ui16PackLen);
		if (iRetVal) {
			g_coLog.WriteLog ("ActivateService: error occurred while processing 'p_pcoIPConn->Send'. error code: '%d'", iRetVal);
			break;
		}
		if (2 <= g_soMainCfg.m_iDebug) {
			ParsePSPack ((SPSRequest*)mcPack, ui16PackLen, 0);
		}

		iRetVal = p_pcoIPConn->Recv (mcPack, sizeof(mcPack));
		if (0 == iRetVal) {
			g_coLog.WriteLog ("ActivateService: connection is closed");
			iRetVal = -1;
			break;
		}
		if (0 > iRetVal) {
			g_coLog.WriteLog ("ActivateService: error occurred while processing 'p_pcoIPConn->Recv'. error code: '%d'", iRetVal);
			break;
		}
		iRetVal = ParsePSPack ((SPSRequest*)mcPack, iRetVal);
	} while (0);

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

int AccountLogoff (
	const SSessionInfo *p_pcsoSessInfo,
	CIPConnector *p_pcoIPConn)
{
	int iRetVal = 0;
	char mcPack[0x10000];
	__uint16_t ui16PackLen;
	__uint16_t ui16ValueLen;
	CPSPacket coPSPack;
	SPSRequest *psoReq;

	ENTER_ROUT;

	do {
		psoReq = (SPSRequest*)mcPack;

		// ищем конфигурацию локации
		std::map<std::string,CConfig*>::iterator iterLocConf;
		CConfig *pcoLocConf;
		iterLocConf = g_mapLocationConf.find (p_pcsoSessInfo->m_mcLocation);
		// если конфигурация локации не найдена
		if (iterLocConf == g_mapLocationConf.end()) {
			g_coLog.WriteLog ("AccountLogoff: Location '%s' configuration not found", p_pcsoSessInfo->m_mcLocation);
			iRetVal = -1;
			break;
		}
		pcoLocConf = iterLocConf->second;

		iRetVal = SetCommonCoASensorAttr (psoReq, sizeof (mcPack), p_pcsoSessInfo, pcoLocConf, p_pcoIPConn);
		if (iRetVal) {
			g_coLog.WriteLog ("AccountLogoff: SetCommonCoASensorAttr: error: code: '%d'", iRetVal);
			break;
		}

		// добавляем атрибут PS_COMMAND
		ui16ValueLen = strlen (CMD_ACCNT_LOGOFF);
		ui16PackLen = coPSPack.AddAttr (psoReq, sizeof(mcPack), PS_COMMAND, CMD_ACCNT_LOGOFF, ui16ValueLen, 0);

		iRetVal = p_pcoIPConn->Send (mcPack, ui16PackLen);
		if (iRetVal) {
			iRetVal = errno;
			g_coLog.WriteLog ("AccountLogoff: error occurred while processing 'p_pcoIPConn->Send'. error code: '%d'", iRetVal);
			break;
		}
		if (2 <= g_soMainCfg.m_iDebug) {
			ParsePSPack ((SPSRequest*)mcPack, ui16PackLen, 0);
		}

		iRetVal = p_pcoIPConn->Recv (mcPack, sizeof(mcPack));
		if (0 == iRetVal) {
			iRetVal = -1;
			g_coLog.WriteLog ("AccountLogoff: connection is closed");
			break;
		}
		if (0 > iRetVal) {
			g_coLog.WriteLog ("AccountLogoff: error occurred while processing 'p_pcoIPConn->Recv'. error code: '%d'", iRetVal);
			break;
		}
		iRetVal = ParsePSPack ((SPSRequest*)mcPack, iRetVal);
	} while (0);

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

int CheckSession (const SSessionInfo *p_pcsoSessInfo, CIPConnector *p_pcoIPConn, otl_connect &p_coDBConn)
{
	int iRetVal = 0;
	int iFnRes;
	char mcPack[0x10000];
	int iValueLen;
	CPSPacket coPSPack;
	SPSRequest *psoReq;
	__uint16_t ui16PackLen;
	__uint16_t ui16AttrLen;

	ENTER_ROUT;

	do {
		psoReq = (SPSRequest*)mcPack;

		// ищем конфигурацию локации
		std::map<std::string,CConfig*>::iterator iterLocConf;
		iterLocConf = g_mapLocationConf.find (p_pcsoSessInfo->m_mcLocation);
		CConfig *pcoLocConf;
		// если конфигурация локации не найдена
		if (iterLocConf == g_mapLocationConf.end()) {
			g_coLog.WriteLog ("CheckSession: Location '%s' configuration not found", p_pcsoSessInfo->m_mcLocation);
			iRetVal = -1;
			break;
		}
		pcoLocConf = iterLocConf->second;

		iRetVal = SetCommonCoASensorAttr (psoReq, sizeof (mcPack), p_pcsoSessInfo, pcoLocConf, p_pcoIPConn);
		if (iRetVal) {
			g_coLog.WriteLog ("CheckSession: SetCommonCoASensorAttr: error: code: '%d'", iRetVal);
			break;
		}

		// добавляем атрибут PS_COMMAND
		ui16AttrLen = strlen (CMD_SESSION_QUERY);
		ui16PackLen = coPSPack.AddAttr (psoReq, sizeof(mcPack), PS_COMMAND, CMD_SESSION_QUERY, ui16AttrLen, 0);

		iRetVal = p_pcoIPConn->Send (mcPack, ui16PackLen);
		if (iRetVal) {
			iRetVal = errno;
			g_coLog.WriteLog ("CheckSession: error occurred while processing 'p_pcoIPConn->Send'. error code: '%d'", iRetVal);
			break;
		}
		if (2 <= g_soMainCfg.m_iDebug) {
			ParsePSPack ((SPSRequest*) mcPack, ui16PackLen, 0);
		}

		iRetVal = p_pcoIPConn->Recv (mcPack, sizeof(mcPack));
		if (0 == iRetVal) {
			iRetVal = -1;
			g_coLog.WriteLog ("CheckSession: connection is closed");
			break;
		}
		if (0 > iRetVal) {
			g_coLog.WriteLog ("CheckSession: error occurred while processing 'p_pcoIPConn->Recv'. error code: '%d'", iRetVal);
			break;
		}
		iRetVal = ParsePSPack ((SPSRequest*)mcPack, iRetVal);
		/* если сессия не найдена */
		if (-45 == iRetVal) {
			iFnRes = FixStuckSession (p_pcsoSessInfo, p_coDBConn);
			if (iFnRes) {
				iRetVal = -1;
				break;
			}
			iFnRes = FixStuckSession (p_pcsoSessInfo, p_coDBConn, true);
			if (iFnRes) {
				iRetVal = -1;
				break;
			}
		}
	} while (0);

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

int ParsePSPack (const SPSRequest *p_pcsoResp, size_t p_stRespLen, int p_iFindResult)
{
	int iRetVal = 0;
	SPSReqAttr *psoAttr;
	__uint16_t ui16ValueLen;
	CPSPacket coPSPack;
	char mcValue[32];
	std::multimap<__uint16_t,SPSReqAttr*> mmapAttrList;
	std::multimap<__uint16_t,SPSReqAttr*>::iterator iterAttrList;

	ENTER_ROUT;

	do {
		iRetVal = coPSPack.Parse (p_pcsoResp, p_stRespLen, mmapAttrList);
		if (iRetVal) {
			g_coLog.WriteLog ("ParsePSPack: parsing failed");
			break;
		}
		if (2 <= g_soMainCfg.m_iDebug) {
			g_coLog.WriteLog ("%sPacket number: '%d'; Packet type: '0x%04x'; Packet size: '%d'", FOOT(g_soMainCfg.m_iDCD), ntohl (p_pcsoResp->m_uiReqNum), ntohs (p_pcsoResp->m_usReqType), ntohs (p_pcsoResp->m_usPackLen));
		}
		iterAttrList = mmapAttrList.find (PS_RESULT);
		if (iterAttrList != mmapAttrList.end()) {
			psoAttr = iterAttrList->second;
			ui16ValueLen = ntohs (psoAttr->m_usAttrLen) - sizeof(*psoAttr);
			ui16ValueLen = ui16ValueLen >= sizeof(mcValue) ? sizeof(mcValue) - 1 : ui16ValueLen;
			memcpy (mcValue, (char*)psoAttr + sizeof(*psoAttr), ui16ValueLen);
			mcValue[ui16ValueLen] = '\0';
			iRetVal = atol (mcValue);
		} else {
			if (p_iFindResult) {
				g_coLog.WriteLog ("ParsePSPack: 'PS_RESULT' not found");
				iRetVal = -1;
				break;
			}
		}
	} while (0);

	coPSPack.EraseAttrList (mmapAttrList);

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}
