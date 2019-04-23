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

#include "utils/coacommon.h"
#include "utils/config/config.h"
#include "utils/log/log.h"
#include "utils/pspacket/pspacket.h"
#include "coam.h"

/* глобальные переменные */
CConfig g_coConf;
CLog g_coLog;
SMainConfig g_soMainCfg;

static std::map<std::string,CConfig*> g_mapLocationConf;
static std::map<std::string,std::string> g_mapNASList;
static unsigned int g_uiReqNum = 0;

static int GetNASLocation( const std::string &p_strNASIPAddress, std::string &p_strLocation );

int LoadConf (const char *p_pszConfDir) {
	int iRetVal = 0;
	std::string strConfDir;
	std::string strConfFile;

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

	return iRetVal;
}

int InitCoAManager ()
{
	int iRetVal = 0;

	do {
		std::vector<std::string> vectConfParam;
		std::string strConfParam;

		iRetVal = g_coConf.GetParamValue ("log_file_mask", strConfParam);
		if (iRetVal) {
			UTL_LOG_F(g_coLog, "Log file mask not defined");
			break;
		}
		/* инициализация логгера */
		iRetVal = g_coLog.Init (strConfParam.c_str());
		if (iRetVal) {
			UTL_LOG_F(g_coLog, "can not initialize log writer: code: '%d'", iRetVal);
			break;
		}
		/* изменение пользователя и группы владельца демона */
		ChangeOSUser ();

		std::string strDBPoolSize;
		int iDBPoolSize;
		int iFnRes;

		const char *pcszConfParam = "db_pool_size";
		iFnRes = g_coConf.GetParamValue( pcszConfParam, strDBPoolSize );
		if( iFnRes || 0 == strDBPoolSize.length() ) {
			UTL_LOG_F( g_coLog, "dbpool: configuration parameter '%s' not defined", pcszConfParam );
		} else {
			iDBPoolSize = atoi( strDBPoolSize.c_str() );
		}

		std::string strDBUser, strDBPswd, strDBDescr;

		pcszConfParam = "db_user";
		iFnRes = g_coConf.GetParamValue( pcszConfParam, strDBUser );
		if( iFnRes || 0 == strDBUser.length() ) {
			UTL_LOG_F( g_coLog, "dbpool: configuration parameter '%s' not defined", pcszConfParam );
		}

		/* запрашиваем пароль пользователя БД из конфигурации */
		pcszConfParam = "db_pswd";
		iFnRes = g_coConf.GetParamValue( pcszConfParam, strDBPswd );
		if( iFnRes || 0 == strDBPswd.length() ) {
			UTL_LOG_F( g_coLog, "dbpool: configuration parameter '%s' not defined", pcszConfParam );
		}

		/* запрашиваем дескриптор БД из конфигурации */
		pcszConfParam = "db_descr";
		iFnRes = g_coConf.GetParamValue( pcszConfParam, strDBDescr );
		if( iFnRes || 0 == strDBDescr.length() ) {
			UTL_LOG_F( g_coLog, "dbpool: configuration parameter '%s' not defined", pcszConfParam );
		}

		/* инициализация пула подклчений к БД */
		iRetVal = db_pool_init(&g_coLog, strDBUser, strDBPswd, strDBDescr, iDBPoolSize );
		if (iRetVal) {
			UTL_LOG_F(g_coLog, "can not initialize DB pool");
		}
		/* создание списка NASов */
		iRetVal = CreateNASList (&g_mapNASList);
		if (iRetVal) {
			break;
		}
		/* инициализация пула потоков */
		iRetVal = InitThreadPool ();
		if (iRetVal) {
			break;
		}
	} while (0);

	return iRetVal;
}

int DeInitCoAManager () {
	int iRetVal = 0;

	db_pool_deinit();

	return iRetVal;
}

void ChangeOSUser ()
{
	int iFnRes;
	const char *pszUser, *pszGroup;
	passwd *psoPswd;
	group *psoGroup;
	gid_t idUser, idGroup;
	std::string strVal;

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
}

int ConnectCoASensor (CIPConnector &p_coIPConn) {
	int iRetVal = 0;
	std::vector<std::string> vectValList;
	const char *pcszConfParam;	// значение параметра из конфигурации
	const char *pcszHostName;	// имя удаленного хоста
	uint16_t ui16Port;			// порт удаленного хоста
	int iProtoType;				// тип протокола взаимодействия с удаленным хостом
	int iFnRes;

	do {
		// выбираем сведения о сетевом протоколе
		pcszConfParam = "coa_sensor_proto";
		iRetVal = g_coConf.GetParamValue (pcszConfParam, vectValList);
		if (iRetVal) {
			iRetVal = -1;
			UTL_LOG_F(g_coLog, "configuration parameter '%s' not found",
				pcszConfParam);
			break;
		}
		if (0 == stricmp ("TCP", vectValList[0].c_str())) {
			iProtoType = IPPROTO_TCP;
		} else if (0 == stricmp ("UDP", vectValList[0].c_str())) {
			iProtoType = IPPROTO_UDP;
		} else {
			iRetVal = -1;
			UTL_LOG_F(
				g_coLog,
				"configuration parameter '%s' containts unsuppurted value '%s'",
				pcszConfParam,
				vectValList[0].c_str());
			break;
		}
		// выбираем имя хоста CoA-сенсора
		vectValList.clear();
		pcszConfParam = "coa_sensor_addr";
		iRetVal = g_coConf.GetParamValue (pcszConfParam, vectValList);
		if (iRetVal) {
			UTL_LOG_F(
				g_coLog,
				"configuration parameter '%s' not found",
				pcszConfParam);
			break;
		}
		pcszHostName = vectValList[0].c_str();
		// выбираем порт CoASensor-а
		vectValList.clear();
		pcszConfParam = "coa_sensor_port";
		iRetVal = g_coConf.GetParamValue (pcszConfParam, vectValList);
		if (iRetVal) {
			UTL_LOG_F(
				g_coLog,
				"configuration parameter '%s' not found",
				pcszConfParam);
			break;
		}
		ui16Port = atol (vectValList[0].c_str());

		/* подключаемся к удаленному хосту */
		iFnRes = p_coIPConn.Connect (pcszHostName, ui16Port, iProtoType);
		if (iFnRes) {
			iRetVal = -1;
			UTL_LOG_F(
				g_coLog,
				"can't connect to CoASensor");
			break;
		}
	} while (0);

	return iRetVal;
}

int RequestTimer () {
	int iRetVal = 0;

	MainWork ();

	return iRetVal;
}

std::multimap<std::string, SSessionInfoFull> g_mmapSessionListFull;

int MainWork ()
{
	int iRetVal = 0;
	std::vector<SRefreshRecord> vectSubscriberList;
	std::vector<SRefreshRecord>::iterator iterSubscr;

	do {
		/* загружаем список абонентов */
		iRetVal = loadRefreshQueue (&vectSubscriberList);
		if (iRetVal) {
			break;
		}
		if (vectSubscriberList.size() > 1000) {
			iRetVal = CreateSessionListFull(g_mmapSessionListFull);
			if (iRetVal) {
				g_mmapSessionListFull.clear();
			}
		}
		/* обходим список абонентов */
		if( 10 <= vectSubscriberList.size() ) {
			UTL_LOG_N( g_coLog, "processing loop is started: queue length: '%u'", vectSubscriberList.size() );
		}
		iterSubscr = vectSubscriberList.begin();
		while (iterSubscr != vectSubscriberList.end()) {
			/* обрабатыаем учетную запись абонента */
			iRetVal = ThreadManager (*iterSubscr);
			if (iRetVal) {
				break;
			}
			++iterSubscr;
		}
		if( 10 <= vectSubscriberList.size() ) {
			UTL_LOG_N( g_coLog, "processing loop completed" );
		}
	} while (0);

	g_mmapSessionListFull.clear();

	return iRetVal;
}

bool operator < (const SSessionInfo &soLeft, const SSessionInfo &soRight) {
	int iFnRes;

	iFnRes = soLeft.m_strUserName == soRight.m_strUserName;
	if (0 > iFnRes) {
		return true;
	}
	if (0 != iFnRes) {
		return false;
	}

	iFnRes = soLeft.m_strNASIPAddress == soRight.m_strNASIPAddress;
	if (0 > iFnRes) {
		return true;
	}
	if (0 != iFnRes) {
		return false;
	}

	iFnRes = soLeft.m_strSessionId == soRight.m_strSessionId;
	if (0 > iFnRes) {
		return true;
	}
	if (0 != iFnRes) {
		return false;
	}

	iFnRes = soLeft.m_strLocation == soRight.m_strLocation;
	if (0 > iFnRes) {
		return true;
	}

	return false;
}

bool operator < (const SPolicyInfo &soLeft, const SPolicyInfo &soRight) {
	int iFnRes;

	iFnRes = soLeft.m_strUserName.compare(soRight.m_strUserName);
	if (0 > iFnRes) {
		return true;
	}
	if (0 != iFnRes) {
		return false;
	}

	iFnRes = soLeft.m_strLocation.compare(soRight.m_strLocation);
	if (0 > iFnRes) {
		return true;
	}

	return false;
}

bool operator < (const SPolicyDetail &soLeft, const SPolicyDetail &soRight) {
	int iFnRes;

	iFnRes = soLeft.m_strAttr.compare(soRight.m_strAttr);
	if (0 > iFnRes) {
		return true;
	}
	if (0 != iFnRes) {
		return false;
	}

	iFnRes = soLeft.m_strValue.compare(soRight.m_strValue);
	if (0 > iFnRes) {
		return true;
	}

	return false;
}

int parseSessionIdIdentifier( const std::string &p_strIdentifier, std::string &p_strSessionId, std::string &p_strNASIPAddress )
{
	int iRetVal = 0;
	size_t stAerobasePos;

	stAerobasePos = p_strIdentifier.find( '@' );
	if( std::string::npos != stAerobasePos ) {
		p_strSessionId.assign( p_strIdentifier, 0, stAerobasePos );
		p_strNASIPAddress.assign( p_strIdentifier, stAerobasePos + 1, std::string::npos );
		/* минимальная проверка правильности значений */
		if( 0 != p_strSessionId.length() && 0 != p_strNASIPAddress.length() ) {
		} else {
			iRetVal = EINVAL;
			if( 0 == p_strSessionId.length() ) {
				UTL_LOG_E( g_coLog, "Session-Id must be non-zero length" );
			}
			if( 0 == p_strNASIPAddress.length() ) {
				UTL_LOG_E( g_coLog, "NAS-IP-Address must be non-zero length" );
			}
		}
	} else {
		UTL_LOG_E( g_coLog, "invalid session_id format: '%s': proper session id format is <session-id>@<NAS-IP-Address>", p_strIdentifier.c_str() );
		iRetVal = EINVAL;
	}

	return iRetVal;
}

int OperateRefreshRecord (
	const SRefreshRecord &p_soRefreshRecord,
	CIPConnector *p_pcoIPConn,
	otl_connect &p_coDBConn)
{
	int iRetVal = 0;
	CTimeMeasurer coTM;
	std::map<std::string, int> mapServiceList;
	std::map<SSessionInfo,std::map<std::string,int> > mapSessionList; /* список сессий подписчика */
	std::map<SSessionInfo,std::map<std::string,int> >::iterator iterSession; /* итератор списка сессий подписчика */
	std::string strWhatWasDone;

	UTL_LOG_D( g_coLog,
			   "identifier type: '%s'; identifier: '%s'; action: '%s'",
			   p_soRefreshRecord.m_strIdentifierType.c_str(), p_soRefreshRecord.m_strIdentifier.c_str(), p_soRefreshRecord.m_strAction.c_str() );

	do {
		if( 0 == p_soRefreshRecord.m_strIdentifierType.compare( ID_TYPE_SESSION_ID ) ) {
			/* тип идентификатора Session-Id */
			SSessionInfo soSessInfo;

			if( 0 == parseSessionIdIdentifier( p_soRefreshRecord.m_strIdentifier, soSessInfo.m_strSessionId, soSessInfo.m_strNASIPAddress ) ) {
			} else {
				iRetVal = EINVAL;
				break;
			}

			OperateSessionInfo( &mapSessionList, soSessInfo, NULL );
		} else if( 0 == p_soRefreshRecord.m_strIdentifierType.compare( ID_TYPE_SUBSCRIBER_ID ) ) {
			/* тип идентификатра Subscriber-Id */
			/* загружаем список сессий подписчика */
			iRetVal = CreateSessionList( p_soRefreshRecord.m_strIdentifier, &mapSessionList, p_coDBConn );
			if( 0 == iRetVal ) {
			} else {
				/* если не удалось загрузить список сессий подписчика */
				break;
			}
		} else {
			UTL_LOG_E( g_coLog, "unsupported identifier type: '%s'", p_soRefreshRecord.m_strIdentifierType.c_str() );
			iRetVal = EINVAL;
			break;
		}

		iterSession = mapSessionList.begin();

		std::map<SPolicyInfo,std::map<SPolicyDetail,int> > mapPolicyList;

		// обходим все сессии абонента
		while( iterSession != mapSessionList.end() ) {
			OperateSubscriberSession( p_soRefreshRecord, iterSession->first, iterSession->second, mapPolicyList, *p_pcoIPConn, p_coDBConn, strWhatWasDone );
			++iterSession;
		}
	} while (0);

	char mcTimeDiff[32];
	coTM.GetDifference (NULL, mcTimeDiff, sizeof(mcTimeDiff));
	UTL_LOG_N( g_coLog,
			   "identifier type: '%s'; identifier: '%s'; action: '%s': operated in '%s'%s",
			   p_soRefreshRecord.m_strIdentifierType.c_str(), p_soRefreshRecord.m_strIdentifier.c_str(), p_soRefreshRecord.m_strAction.c_str(), mcTimeDiff, strWhatWasDone.c_str() );

	return iRetVal;
}

int OperateSubscriberSession(
	const SRefreshRecord &p_soRefreshRecord,
	const SSessionInfo &p_soSessionInfo,
	std::map<std::string, int> &p_soSessionPolicyList,
	std::map<SPolicyInfo, std::map<SPolicyDetail, int> > &p_mapProfilePolicyList,
	CIPConnector &p_coIPConn,
	otl_connect &p_coDBConn,
	std::string &p_strWhatWasDone )
{
	int iRetVal = 0;

	do {
		SPolicyInfo soPolicyInfo;
		std::map<SPolicyInfo, std::map<SPolicyDetail, int> >::iterator iterProfilePolicy = p_mapProfilePolicyList.end();
		std::map<SPolicyInfo, std::map<SPolicyDetail, int> >::iterator iterProfilePolicyDef = p_mapProfilePolicyList.end();
		int iFnRes;
		char *pszMsg;

		iFnRes = asprintf( &pszMsg, "\r\n\tUserName: '%s'; NASIPAddress: '%s'; SessionID: '%s'; Location: '%s'",
						   p_soSessionInfo.m_strUserName.c_str(),
						   p_soSessionInfo.m_strNASIPAddress.c_str(),
						   p_soSessionInfo.m_strSessionId.c_str(),
						   p_soSessionInfo.m_strLocation.c_str() );
		if( 0 < iFnRes ) {
			p_strWhatWasDone.append( pszMsg );
			free( pszMsg );
		}

		if( 0 == p_soRefreshRecord.m_strAction.compare( ACTION_TYPE_LOGOFF ) ) {
			/* отрабатываем команду 'logoff' */
			iRetVal = AccountLogoff( p_soSessionInfo, &p_coIPConn, p_strWhatWasDone );
			if( 0 == iRetVal ) {
			} else {
				break;
			}
		} else if( 0 == p_soRefreshRecord.m_strAction.compare( ACTION_TYPE_CHECK_SESS ) ) {
			/* отрабатываем команду 'checksession' */
			iRetVal = CheckSession( &( p_soSessionInfo ), &p_coIPConn, p_coDBConn );
			if( iRetVal ) {
				break;
			}
		} else if( 0 == p_soRefreshRecord.m_strAction.compare( ACTION_TYPE_CHECK_POLICY ) || 0 == p_soRefreshRecord.m_strAction.length() ) {
			if( 0 == p_soRefreshRecord.m_strIdentifierType.compare( ID_TYPE_SUBSCRIBER_ID ) ) {
			} else {
				iFnRes = asprintf( &pszMsg, "\r\n\t\terror: action '%s' is not supported for identifier type '%s'", p_soRefreshRecord.m_strAction.c_str(), p_soRefreshRecord.m_strIdentifierType.c_str() );
				if( 0 < iFnRes ) {
					p_strWhatWasDone.append( pszMsg );
					free( pszMsg );
				}
				iRetVal = EINVAL;
				break;
			}
			/* действие по умолчанию */
			/* ищем кешированные политики */
			soPolicyInfo.m_strUserName = p_soSessionInfo.m_strUserName;
			// ищем кешированную политику для локации сервера доступа
			soPolicyInfo.m_strLocation = p_soSessionInfo.m_strLocation;
			iterProfilePolicy = p_mapProfilePolicyList.find( soPolicyInfo );
			// ищем кешированную политику для локации DEFAULT
			if( p_soSessionInfo.m_strLocation.compare( "DEFAULT" ) ) {
				soPolicyInfo.m_strLocation = "DEFAULT";
				iterProfilePolicyDef = p_mapProfilePolicyList.find( soPolicyInfo );
			}
			/* если кешированные политики не найдены запрашиваем их из БД */
			if( iterProfilePolicy == p_mapProfilePolicyList.end() && iterProfilePolicyDef == p_mapProfilePolicyList.end() ) {
				iRetVal = CreatePolicyList( &( p_soSessionInfo ), &p_mapProfilePolicyList, p_coDBConn );
				/* если произошла ошибка при формировании списка политик завершаем работу */
				if( iRetVal ) {
					iFnRes = asprintf( &pszMsg, "\r\n\t\terror: CreatePolicyList failed: code: '%d'", iRetVal );
					if( 0 < iFnRes ) {
						p_strWhatWasDone.append( pszMsg );
						free( pszMsg );
					}
					break;
				}
				/* повторно ищем кешированную политику для локации сервера доступа */
				soPolicyInfo.m_strLocation = p_soSessionInfo.m_strLocation;
				iterProfilePolicy = p_mapProfilePolicyList.find( soPolicyInfo );
				/* повторно ищем кешированную политику для локации DEFAULT */
				if( p_soSessionInfo.m_strLocation.compare( "DEFAULT" ) ) {
					soPolicyInfo.m_strLocation = "DEFAULT";
					iterProfilePolicyDef = p_mapProfilePolicyList.find( soPolicyInfo );
				}
			}
			/* если политики не найдены */
			if( iterProfilePolicy == p_mapProfilePolicyList.end() && iterProfilePolicyDef == p_mapProfilePolicyList.end() ) {
				// политики не найдены, посылаем LogOff
				iRetVal = AccountLogoff( p_soSessionInfo, &p_coIPConn, p_strWhatWasDone );
				if( iRetVal ) {
					break;
				}
			} else {
				/* обрабатываем политики */
				if( iterProfilePolicy != p_mapProfilePolicyList.end() ) {
					SelectActualPolicy( &p_soSessionPolicyList, &( iterProfilePolicy->second ) );
				}
				if( iterProfilePolicyDef != p_mapProfilePolicyList.end() ) {
					SelectActualPolicy( &p_soSessionPolicyList, &( iterProfilePolicyDef->second ) );
				}
				/* отключаем активные неактуальные политики */
				iRetVal = DeactivateNotrelevantPolicy( p_soSessionInfo, p_soSessionPolicyList, p_coIPConn, p_strWhatWasDone );
				if( iRetVal ) {
					break;
				}
				/* включаем неактивные актуальные политики */
				if( iterProfilePolicy != p_mapProfilePolicyList.end() ) {
					iRetVal = ActivateInactivePolicy( p_soSessionInfo, iterProfilePolicy->second, p_coIPConn, p_strWhatWasDone );
					if( iRetVal ) {
						break;
					}
				}
				if( iterProfilePolicyDef != p_mapProfilePolicyList.end() ) {
					iRetVal = ActivateInactivePolicy( p_soSessionInfo, iterProfilePolicyDef->second, p_coIPConn, p_strWhatWasDone );
					if( iRetVal ) {
						break;
					}
				}
			}
			if( iRetVal ) {
				break;
			}
		} else {
			/* неизвестное значение 'action' */
			iFnRes = asprintf( &pszMsg, "\r\n\t\terror: action: '%s' is not supported", p_soRefreshRecord.m_strAction.c_str() );
			if( 0 < iFnRes ) {
				p_strWhatWasDone.append( pszMsg );
				free( pszMsg );
			}
			iRetVal = -1;
		}
	} while( 0 );

	return iRetVal;
}

int DeactivateNotrelevantPolicy (
	const SSessionInfo &p_soSessionInfo,
	std::map<std::string,int> &p_soSessionPolicyList,
	CIPConnector &p_coIPConn,
	std::string &p_strWhatWasDone )
{
	int iRetVal = 0;

	do {
		std::map<std::string,int>::iterator iterSessionPolicy;
		iterSessionPolicy = p_soSessionPolicyList.begin ();
		while (iterSessionPolicy != p_soSessionPolicyList.end ()) {
			if (0 == iterSessionPolicy->second) {
				iRetVal = DeActivateService( &p_soSessionInfo, iterSessionPolicy->first.c_str(), NULL, &p_coIPConn, p_strWhatWasDone );
				if (iRetVal) {
					break;
				}
			}
			++iterSessionPolicy;
		}
	} while (0);

	return iRetVal;
}

int ActivateInactivePolicy(
	const SSessionInfo &p_soSessionInfo,
	std::map<SPolicyDetail, int> &p_mapPolicyDetail,
	CIPConnector &p_coIPConn,
	std::string &p_strWhatWasDone )
{
	int iRetVal = 0;

	do {
		std::map<SPolicyDetail, int>::iterator iterProfilePolicyDetail;
		iterProfilePolicyDetail = p_mapPolicyDetail.begin();
		while( iterProfilePolicyDetail != p_mapPolicyDetail.end() ) {
			if( 0 == iterProfilePolicyDetail->second ) {
				iRetVal = ActivateService( &p_soSessionInfo,
										   iterProfilePolicyDetail->first.m_strValue.c_str(),
										   iterProfilePolicyDetail->first.m_strAttr.c_str(),
										   &p_coIPConn,
										   p_strWhatWasDone );
				if( iRetVal ) {
					break;
				}
			}
			++iterProfilePolicyDetail;
		}
	} while( 0 );

	return iRetVal;
}

int GetNASLocation( const std::string &p_strNASIPAddress, std::string &p_strLocation )
{
	int iRetVal = 0;
	std::map<std::string, std::string>::iterator iterNASList;

	iterNASList = g_mapNASList.find( p_strNASIPAddress );
	if( iterNASList != g_mapNASList.end() ) {
		p_strLocation = iterNASList->second;
	} else {
		iRetVal = -1;
		UTL_LOG_E( g_coLog, "NAS '%s' not found", p_strNASIPAddress.c_str() );
	}

	return iRetVal;
}

bool ModifyValue( std::string &p_strModificator, std::string &p_strValue )
{
	if( 0 != p_strModificator.length() ) {
	} else {
		return false;
	}

	/* for back compatibility: if modifier is not specified assumed modifier as [-]prefix */
	// если длина префикса меньше или равна имени сервиса
	if( p_strModificator[0] != '+' && p_strModificator[p_strModificator.length() - 1] != '+' && p_strModificator[p_strModificator.length() - 1] != '-' ) {
		/* remove [-]prfix */
		if( p_strModificator[0] == '-' ) {
			/* remove modificator */
			p_strModificator.erase( 0, 1 );
		}
		if( 0 == p_strValue.compare( 0, p_strModificator.length(), p_strModificator ) ) {
			// если перфикс и начало имени сервиса совпадают
			// убираем префикс
			p_strValue.erase( 0, p_strModificator.length() );
			// завершаем поиск правила для имени сервиса
			return true;
		}
	}

	if( p_strModificator[p_strModificator.length() - 1] == '-' && p_strModificator.length() <= p_strValue.length() ) {
		/* remove suffix- */
		/*remove modificator */
		p_strModificator.erase( p_strModificator.length() - 1, 1 );
		/* remove suffix */
		p_strValue.erase( p_strValue.length() - p_strModificator.length(), p_strModificator.length() );
	}

	if( p_strModificator[0] == '+' ) {
		/* append as +prefix */
		/* remove modificator */
		p_strModificator.erase( 0, 1 );
		/* append modificator */
		p_strValue.replace( 0, 0, p_strModificator );

		return true;
	}

	if( p_strModificator[p_strModificator.length() - 1] == '+' ) {
		/* append as postfix+ */
		/* remove modificator */
		p_strModificator.erase( p_strModificator.length() - 1, 1 );
		/* append modificator */
		p_strValue.replace( p_strValue.length(), 0, p_strModificator );

		return true;
	}

	return false;
}

int ModifyName(
	const char *p_pszModifyRule,
	std::string &p_strLocation,
	std::string &p_strValue)
{
	int iRetVal = 0;
	std::map<std::string,CConfig*>::iterator iterLocation;
	std::vector<std::string> vectValList;
	std::vector<std::string>::iterator iterValList;
	CConfig *pcoLocConf;

	do {
		iterLocation = g_mapLocationConf.find (p_strLocation);
		if (iterLocation == g_mapLocationConf.end()) {
			UTL_LOG_E(g_coLog, "Location '%s' configuration not found", p_strLocation.c_str());
			iRetVal = -1;
			break;
		}
		pcoLocConf = iterLocation->second;
		if (NULL == pcoLocConf) {
			UTL_LOG_E(g_coLog, "location '%s' configuration not exists", p_strLocation.c_str());
			iRetVal = -2;
			break;
		}
		iRetVal = pcoLocConf->GetParamValue (p_pszModifyRule, vectValList);
		if (iRetVal) {
			break;
		}
		// выбираем первое правило
		iterValList = vectValList.begin();
		// обходим все правила изменения имен сервисов
		while (iterValList != vectValList.end()) {
			UTL_LOG_D( g_coLog, "rule: %s; location: %s; value: %s", p_pszModifyRule, p_strLocation.c_str(), p_strValue.c_str() );
			if( ModifyValue( *iterValList, p_strValue ) ) {
				UTL_LOG_D( g_coLog, "modified value: rule: %s; location: %s; value: %s", p_pszModifyRule, p_strLocation.c_str(), p_strValue.c_str() );
				break;
			}
			++iterValList;
		}
	} while (0);

	return iRetVal;
}

bool Filter(const char *p_pszFilterName, std::string &p_strLocation, std::string &p_strValue)
{
	bool bRetVal = false;
	int iFnRes;
	std::map<std::string,CConfig*>::iterator iterLocation;
	std::vector<std::string> vectValList;
	std::vector<std::string>::iterator iterValList;
	CConfig *pcoLocConf;
	int iValueLen = p_strValue.length();

	do {
		// ищем конфигурацию локации
		iterLocation = g_mapLocationConf.find (p_strLocation);
		if (iterLocation == g_mapLocationConf.end()) {
			UTL_LOG_E(g_coLog, "Location '%s' configuration not found", p_strLocation.c_str());
			bRetVal = true;
			break;
		}
		pcoLocConf = iterLocation->second;
		if (NULL == pcoLocConf) {
			UTL_LOG_E(g_coLog, "Location '%s' configuration not exists", p_strLocation.c_str());
			bRetVal = true;
			break;
		}
		/* выбираем фильтры для локации */
		if (0 != pcoLocConf->GetParamValue (p_pszFilterName, vectValList)) {
			bRetVal = true;
			break;
		}
		// обходим все правила изменения имен сервисов
		for (iterValList = vectValList.begin(); iterValList != vectValList.end(); ++iterValList) {
			// если длина значения больше или равна длине значения фильтра
			if (static_cast<size_t>(iValueLen) >= iterValList->length()) {
				// сравниваем префикс с началом имени сервиса
				iFnRes = p_strValue.compare( 0, iterValList->length(), *iterValList );
				// если перфикс и начало имени сервиса совпадают
				if (0 == iFnRes) {
					bRetVal = true;
					UTL_LOG_D( g_coLog, "value %s matched to filter %s", p_strValue.c_str(), iterValList->c_str() );
					break;
				} else {
					UTL_LOG_D( g_coLog, "value %s NOT matched to filter %s(length: %u)", p_strValue.c_str(), iterValList->c_str(), iterValList->length() );
				}
			}
		}
	} while (0);

	if( ! bRetVal ) {
		UTL_LOG_D( g_coLog, "value was declined by '%s': location: %s; value: %s", p_pszFilterName, p_strLocation.c_str(), p_strValue.c_str() );
	}

	return bRetVal;
}

int SelectActualPolicy (std::map<std::string,int> *p_pmapSessionDetail, std::map<SPolicyDetail,int> *p_pmapPolicyDetail)
{
	int iRetVal = 0;
	std::map<std::string,int>::iterator iterSessionPolicy;
	std::map<SPolicyDetail,int>::iterator iterProfilePolicy;
	int iFnRes;

	iterSessionPolicy = p_pmapSessionDetail->begin();
	while (iterSessionPolicy != p_pmapSessionDetail->end()) {
		iterProfilePolicy = p_pmapPolicyDetail->begin();
		while (iterProfilePolicy != p_pmapPolicyDetail->end()) {
			iFnRes = iterSessionPolicy->first.compare (iterProfilePolicy->first.m_strValue);
			if (0 == iFnRes) {
				iterSessionPolicy->second = 1;
				iterProfilePolicy->second = 1;
				break;
			}
			++iterProfilePolicy;
		}
		++iterSessionPolicy;
	}

	return iRetVal;
}

int SetCommonCoASensorAttr (SPSRequest *p_psoRequest, size_t p_stBufSize, const SSessionInfo *p_pcsoSessionInfo, CConfig *p_pcoConf, CIPConnector *p_pcoIPConn)
{
	int iRetVal = 0;
	int iFnRes;
	CPSPacket coPSPack;
	__uint16_t ui16ValueLen;

	do {
		if (2 != p_pcoIPConn->GetStatus ()) {
			iFnRes = ConnectCoASensor (*p_pcoIPConn);
			if (iFnRes) {
				iRetVal = iFnRes;
				UTL_LOG_E(g_coLog, "can not connect to CoASensor");
				break;
			}
		}

		coPSPack.Init (p_psoRequest, p_stBufSize, g_uiReqNum++, COMMAND_REQ);

		// добавляем атрибут PS_NASIP
		ui16ValueLen = p_pcsoSessionInfo->m_strNASIPAddress.length();
		coPSPack.AddAttr (p_psoRequest, p_stBufSize, PS_NASIP, p_pcsoSessionInfo->m_strNASIPAddress.data(), ui16ValueLen, 0);

		// добавляем атрибут PS_SESSID
		ui16ValueLen = p_pcsoSessionInfo->m_strSessionId.length();
		coPSPack.AddAttr (p_psoRequest, p_stBufSize, PS_SESSID, p_pcsoSessionInfo->m_strSessionId.data(), ui16ValueLen, 0);

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
			coPSPack.AddAttr (p_psoRequest, p_stBufSize, ui16AttrType, pszVal, ui16ValueLen, 0);
		}

	} while (0);

	return iRetVal;
}

int DeActivateService (
	const SSessionInfo *p_pcsoSessInfo,
	const char *p_pcszServiceInfo,
	const char *p_pcszAttr,
	CIPConnector *p_pcoIPConn,
	std::string &p_strWhatWasDone )
{
	int iRetVal = 0;
	char mcPack[0x10000];
	__uint16_t ui16PackLen;
	__uint16_t ui16ValueLen;
	CPSPacket coPSPack;
	SPSRequest *psoReq;
	int iFnRes;
	char *pszMsg;

	do {
		psoReq = (SPSRequest*)mcPack;

		// ищем конфигурацию локации
		std::map<std::string,CConfig*>::iterator iterLocConf;
		CConfig *pcoLocConf;
		iterLocConf = g_mapLocationConf.find (p_pcsoSessInfo->m_strLocation);
		// если конфигурация локации не найдена
		if (iterLocConf == g_mapLocationConf.end()) {
			UTL_LOG_E(g_coLog, "Location '%s' configuration not found", p_pcsoSessInfo->m_strLocation.c_str());
			iRetVal = -1;
			break;
		}
		pcoLocConf = iterLocConf->second;

		iRetVal = SetCommonCoASensorAttr (psoReq, sizeof (mcPack), p_pcsoSessInfo, pcoLocConf, p_pcoIPConn);
		if (iRetVal) {
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
			UTL_LOG_E(g_coLog, "config parameter '%s' not found", pcszConfParamName);
			break;
		}
		// или его значение не задано
		if (0 == strCmd.length()) {
			UTL_LOG_E(g_coLog, "Config parameter '%s' not defined", pcszConfParamName);
			iRetVal = -1;
			break;
		}
		// запрашиваем значение конфигурационного параметра
		pcszConfParamName = "use_policy_attr_name";
		iRetVal = pcoLocConf->GetParamValue (pcszConfParamName, strUseAttrName);
		// если параметр не найден
		if (iRetVal) {
			UTL_LOG_E(g_coLog, "Config parameter '%s' not found", pcszConfParamName);
			break;
		}
		if (0 == strUseAttrName.compare ("yes") && NULL != p_pcszAttr) {
			ui16ValueLen = snprintf (mcCommand, sizeof(mcCommand), "%s=%s=%s", strCmd.c_str(), p_pcszAttr, p_pcszServiceInfo);
		} else {
			ui16ValueLen = snprintf (mcCommand, sizeof(mcCommand), "%s=%s", strCmd.c_str(), p_pcszServiceInfo);
		}
		if (ui16ValueLen > sizeof(mcCommand) - 1) {
			UTL_LOG_E(g_coLog, "buffer 'mcCommand' too small to store value: needed size is: '%u'", ui16ValueLen);
			iRetVal = -1;
			break;
		}
		ui16PackLen = coPSPack.AddAttr (psoReq, sizeof(mcPack), PS_COMMAND, mcCommand, ui16ValueLen, 0);

		iRetVal = p_pcoIPConn->Send (mcPack, ui16PackLen);
		if (iRetVal) {
			UTL_LOG_E(g_coLog, "'p_pcoIPConn->Send': error code: '%d'", iRetVal);
			break;
		}

		iRetVal = p_pcoIPConn->Recv (mcPack, sizeof(mcPack));
		if (0 == iRetVal) {
			UTL_LOG_E(g_coLog, "connection is closed");
			iRetVal = -1;
			break;
		}
		if (0 > iRetVal) {
			UTL_LOG_E(g_coLog, "'p_pcoIPConn->Recv': error code: '%d'", iRetVal);
			break;
		}
		iRetVal = ParsePSPack ((SPSRequest*)mcPack, iRetVal);
	} while (0);

	switch (iRetVal) {
	case 0:
	case -45:
		iFnRes = asprintf( &pszMsg, "\r\n\t\tservice '%s' was deactivated; result code: '%d'", p_pcszServiceInfo, iRetVal );
		if( 0 < iFnRes ) {
			p_strWhatWasDone.append( pszMsg );
			free( pszMsg );
		}
		iRetVal = 0;
		break;
	default:
		break;
	}

	return iRetVal;
}

int ActivateService(
	const SSessionInfo *p_pcsoSessInfo,
	const char *p_pcszServiceInfo,
	const char *p_pcszAttr,
	CIPConnector *p_pcoIPConn,
	std::string &p_strWhatWasDone )
{
	int iRetVal = 0;
	char mcPack[0x10000];
	__uint16_t ui16PackLen;
	__uint16_t ui16ValueLen;
	CPSPacket coPSPack;
	SPSRequest *psoReq;
	int iFnRes;
	char *pszMsg;

	do {
		psoReq = ( SPSRequest* ) mcPack;

		// ищем конфигурацию локации
		std::map<std::string, CConfig*>::iterator iterLocConf;
		CConfig *pcoLocConf;
		iterLocConf = g_mapLocationConf.find( p_pcsoSessInfo->m_strLocation );
		// если конфигурация локации не найдена
		if( iterLocConf == g_mapLocationConf.end() ) {
			UTL_LOG_E( g_coLog, "Location '%s' configuration not found", p_pcsoSessInfo->m_strLocation.c_str() );
			iRetVal = -1;
			break;
		}
		pcoLocConf = iterLocConf->second;

		iRetVal = SetCommonCoASensorAttr( psoReq, sizeof( mcPack ), p_pcsoSessInfo, pcoLocConf, p_pcoIPConn );
		if( iRetVal ) {
			break;
		}

		// добавляем атрибут PS_COMMAND
		const char *pcszConfParamName;
		char mcCommand[0x400];
		std::string strCmd;
		std::string strUserAttrName;

		pcszConfParamName = "activation";
		// запрашиваем значение конфигурационного параметра
		iRetVal = pcoLocConf->GetParamValue( pcszConfParamName, strCmd );
		// если параметр не найден
		if( iRetVal ) {
			UTL_LOG_E( g_coLog, "Config parameter '%s' not found", pcszConfParamName );
			break;
		}
		// или его значение не задано
		if( 0 == strCmd.length() ) {
			UTL_LOG_E( g_coLog, "Config parameter '%s' not defined", pcszConfParamName );
			iRetVal = -1;
			break;
		}
		// запрашиваем значение конфигурационного параметра
		pcszConfParamName = "use_policy_attr_name";
		iRetVal = pcoLocConf->GetParamValue( pcszConfParamName, strUserAttrName );
		// если параметр не найден
		if( iRetVal ) {
			UTL_LOG_E( g_coLog, "Config parameter '%s' not found", pcszConfParamName );
			break;
		}
		if( 0 == strUserAttrName.compare( "yes" ) && NULL != p_pcszAttr ) {
			ui16ValueLen = snprintf( mcCommand, sizeof( mcCommand ), "%s=%s=%s", strCmd.c_str(), p_pcszAttr, p_pcszServiceInfo );
		} else {
			ui16ValueLen = snprintf( mcCommand, sizeof( mcCommand ), "%s=%s", strCmd.c_str(), p_pcszServiceInfo );
		}
		if( ui16ValueLen > sizeof( mcCommand ) - 1 ) {
			UTL_LOG_E( g_coLog, "buffer 'mcCommand' too small to store value: needed size is: '%u'", ui16ValueLen );
			iRetVal = -1;
			break;
		}
		ui16PackLen = coPSPack.AddAttr( psoReq, sizeof( mcPack ), PS_COMMAND, mcCommand, ui16ValueLen, 0 );

		iRetVal = p_pcoIPConn->Send( mcPack, ui16PackLen );
		if( iRetVal ) {
			UTL_LOG_E( g_coLog, "'p_pcoIPConn->Send': error code: '%d'", iRetVal );
			break;
		}

		iRetVal = p_pcoIPConn->Recv( mcPack, sizeof( mcPack ) );
		if( 0 == iRetVal ) {
			UTL_LOG_E( g_coLog, "connection is closed" );
			iRetVal = -1;
			break;
		}
		if( 0 > iRetVal ) {
			UTL_LOG_E( g_coLog, "'p_pcoIPConn->Recv': error code: '%d'", iRetVal );
			break;
		}
		iRetVal = ParsePSPack( ( SPSRequest* ) mcPack, iRetVal );
	} while( 0 );

	switch( iRetVal ) {
		case 0:
		case -45:
			iFnRes = asprintf( &pszMsg, "\r\n\t\tservice '%s' was activated; result code: '%d'", p_pcszServiceInfo, iRetVal );
			if( 0 < iFnRes ) {
				p_strWhatWasDone.append( pszMsg );
				free( pszMsg );
			}
			iRetVal = 0;
			break;
		default:
			break;
	}

	return iRetVal;
}

int AccountLogoff( const SSessionInfo &p_soSessInfo, CIPConnector *p_pcoIPConn, std::string &p_strWhatWasDone )
{
	int iRetVal = 0;
	int iFnRes;
	char *pszMsg;
	char mcPack[0x10000];
	__uint16_t ui16PackLen;
	__uint16_t ui16ValueLen;
	CPSPacket coPSPack;
	SPSRequest *psoReq;

	do {
		psoReq = ( SPSRequest* ) mcPack;

		// ищем конфигурацию локации
		std::map<std::string, CConfig*>::iterator iterLocConf;
		CConfig *pcoLocConf;
		iterLocConf = g_mapLocationConf.find( p_soSessInfo.m_strLocation );
		// если конфигурация локации не найдена
		if( iterLocConf != g_mapLocationConf.end() ) {
		} else {
			iFnRes = asprintf( &pszMsg, "\r\n\t\tLocation '%s' configuration not found",
							   p_soSessInfo.m_strLocation.c_str() );
			if( 0 < iFnRes ) {
				p_strWhatWasDone.append( pszMsg );
				free( pszMsg );
			}
			iRetVal = -1;
			break;
		}
		pcoLocConf = iterLocConf->second;

		iRetVal = SetCommonCoASensorAttr( psoReq, sizeof( mcPack ), &p_soSessInfo, pcoLocConf, p_pcoIPConn );
		if( 0 == iRetVal ) {
		} else {
			break;
		}

		// добавляем атрибут PS_COMMAND
		ui16ValueLen = strlen( CMD_ACCNT_LOGOFF );
		ui16PackLen = coPSPack.AddAttr( psoReq, sizeof( mcPack ), PS_COMMAND, CMD_ACCNT_LOGOFF, ui16ValueLen, 0 );

		iRetVal = p_pcoIPConn->Send( mcPack, ui16PackLen );
		if( 0 == iRetVal ) {
		} else {
			iRetVal = errno;
			iFnRes = asprintf( &pszMsg, "\r\n\t\t'p_pcoIPConn->Send'. error code: '%d'",
							   iRetVal );
			if( 0 < iFnRes ) {
				p_strWhatWasDone.append( pszMsg );
				free( pszMsg );
			}
			break;
		}

		iRetVal = p_pcoIPConn->Recv( mcPack, sizeof( mcPack ) );
		if( 0 < iRetVal ) {
			/* команда выполнена успешно */
		} else if( 0 == iRetVal ) {
			/* соединение закрыто сервером */
			iRetVal = -1;
			p_strWhatWasDone.append( "\r\n\t\tconnection is closed" );
			break;
		} else if( 0 > iRetVal ) {
			/* при выполнении команды произошла ошибка */
			iFnRes = asprintf( &pszMsg, "\r\n\t\t'p_pcoIPConn->Recv'. error code: '%d'", iRetVal );
			if( 0 < iFnRes ) {
				p_strWhatWasDone.append( pszMsg );
				free( pszMsg );
			}
			break;
		}
		iRetVal = ParsePSPack( ( SPSRequest* ) mcPack, iRetVal );
	} while( 0 );

	switch( iRetVal ) {
		case 0:
		case -45:
			iFnRes = asprintf( &pszMsg, "\r\n\t\tuser was disconnected; result code: '%d'", iRetVal );
			if( 0 < iFnRes ) {
				p_strWhatWasDone.append( pszMsg );
				free( pszMsg );
			}
			iRetVal = 0;
			break;
		default:
			break;
	}

	return iRetVal;
}

int CheckSession (
	const SSessionInfo *p_pcsoSessInfo,
	CIPConnector *p_pcoIPConn,
	otl_connect &p_coDBConn)
{
	int iRetVal = 0;
	int iFnRes;
	char mcPack[0x10000];
	CPSPacket coPSPack;
	SPSRequest *psoReq;
	__uint16_t ui16PackLen;
	__uint16_t ui16AttrLen;

	do {
		psoReq = (SPSRequest*)mcPack;

		// ищем конфигурацию локации
		std::map<std::string,CConfig*>::iterator iterLocConf;
		iterLocConf = g_mapLocationConf.find (p_pcsoSessInfo->m_strLocation);
		CConfig *pcoLocConf;
		// если конфигурация локации не найдена
		if (iterLocConf == g_mapLocationConf.end()) {
			UTL_LOG_E(g_coLog, "Location '%s' configuration not found", p_pcsoSessInfo->m_strLocation.c_str());
			iRetVal = -1;
			break;
		}
		pcoLocConf = iterLocConf->second;

		iRetVal = SetCommonCoASensorAttr (psoReq, sizeof (mcPack), p_pcsoSessInfo, pcoLocConf, p_pcoIPConn);
		if (iRetVal) {
			break;
		}

		// добавляем атрибут PS_COMMAND
		ui16AttrLen = strlen (CMD_SESSION_QUERY);
		ui16PackLen = coPSPack.AddAttr (psoReq, sizeof(mcPack), PS_COMMAND, CMD_SESSION_QUERY, ui16AttrLen, 0);

		iRetVal = p_pcoIPConn->Send (mcPack, ui16PackLen);
		if (iRetVal) {
			iRetVal = errno;
			UTL_LOG_E(g_coLog, "error occurred while processing 'p_pcoIPConn->Send'. error code: '%d'", iRetVal);
			break;
		}

		iRetVal = p_pcoIPConn->Recv (mcPack, sizeof(mcPack));
		if (0 == iRetVal) {
			iRetVal = -1;
			UTL_LOG_E(g_coLog, "connection is closed");
			break;
		}
		if (0 > iRetVal) {
			UTL_LOG_E(g_coLog, "error occurred while processing 'p_pcoIPConn->Recv'. error code: '%d'", iRetVal);
			break;
		}
		iRetVal = ParsePSPack ((SPSRequest*)mcPack, iRetVal);
		/* если сессия не найдена */
		switch (iRetVal) {
		case -45:
			/* фиксируем зависшую сессию (обычный запрос) */
			iFnRes = FixStuckSession (p_pcsoSessInfo, p_coDBConn);
			if (iFnRes) {
				iRetVal = -1;
				break;
			}
			/* фиксируем зависшую сессию (опциональный запрос) */
			iFnRes = FixStuckSession (p_pcsoSessInfo, p_coDBConn, true);
			if (iFnRes) {
				iRetVal = -1;
				break;
			}
			UTL_LOG_N(
				g_coLog,
				"UserName: '%s'; NASIPAddress: '%s'; SessionID: '%s'; user session fixed",
				p_pcsoSessInfo->m_strUserName.c_str(),
				p_pcsoSessInfo->m_strNASIPAddress.c_str(),
				p_pcsoSessInfo->m_strSessionId.c_str());
			break;
		case 0:
			UTL_LOG_N(
				g_coLog,
				"UserName: '%s'; NASIPAddress: '%s'; SessionID: '%s'; user session is active - nothing to do",
				p_pcsoSessInfo->m_strUserName.c_str(),
				p_pcsoSessInfo->m_strNASIPAddress.c_str(),
				p_pcsoSessInfo->m_strSessionId.c_str());
			break;
		default:
			UTL_LOG_E(
				g_coLog,
				"UserName: '%s'; NASIPAddress: '%s'; SessionID: '%s': error code: '%d'",
				p_pcsoSessInfo->m_strUserName.c_str(),
				p_pcsoSessInfo->m_strNASIPAddress.c_str(),
				p_pcsoSessInfo->m_strSessionId.c_str(),
				iRetVal);
			break;
		}
	} while (0);

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

	do {
		iRetVal = coPSPack.Parse (p_pcsoResp, p_stRespLen, mmapAttrList);
		if (iRetVal) {
			UTL_LOG_E(g_coLog, "parsing failed");
			break;
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
				UTL_LOG_E(g_coLog, "'PS_RESULT' not found");
				iRetVal = -1;
				break;
			}
		}
	} while (0);

	coPSPack.EraseAttrList (mmapAttrList);

	return iRetVal;
}

int OperateSessionInfo(
	std::map<SSessionInfo, std::map<std::string, int> > *p_pmapSessList,
	SSessionInfo &p_soSessInfo,
	std::string *p_pstrServiceInfo )
{
	int iRetVal = 0;
	int iFnRes;

	do {
		std::map<SSessionInfo, std::map<std::string, int> >::iterator iterSessInfo;

		iFnRes = GetNASLocation( p_soSessInfo.m_strNASIPAddress, p_soSessInfo.m_strLocation );
		/* ���� NAS �� ������ */
		if( iFnRes ) {
			p_soSessInfo.m_strLocation = "DEFAULT";
		}
		if( NULL != p_pstrServiceInfo ) {
			ModifyName( "sess_info_pref", p_soSessInfo.m_strLocation, *p_pstrServiceInfo ); /* it should be deprecated */
			ModifyName( "activeServiceName_modifier", p_soSessInfo.m_strLocation, *p_pstrServiceInfo );
		}

		std::string strServiceName;

		if( NULL != p_pstrServiceInfo ) {
			strServiceName.assign( *p_pstrServiceInfo );
		}
		iterSessInfo = p_pmapSessList->find( p_soSessInfo );
		if( iterSessInfo != p_pmapSessList->end() ) {
			iterSessInfo->second.insert(
				std::make_pair(
					strServiceName,
					0 ) );
		} else {
			std::map<std::string, int> mapTmp;
			mapTmp.insert(
				std::make_pair(
					strServiceName,
					0 ) );
			p_pmapSessList->insert(
				std::make_pair(
					p_soSessInfo,
					mapTmp ) );
		}
	} while( 0 );

	return iRetVal;
}
