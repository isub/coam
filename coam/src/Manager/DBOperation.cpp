#include "../../../utils/config/Config.h"
#include "../../../utils/log/Log.h"

#include "coam.h"

/* шаблон строки подключения к БД */
static char g_mcRadDBConnTempl[] = "%s/%s@(DESCRIPTION=(ADDRESS=(PROTOCOL=TCP)(HOST=%s)(PORT=%s))(CONNECT_DATA=(SERVICE_NAME=%s)))";

extern CConfig g_coConf;
extern CLog g_coLog;
extern SMainConfig g_soMainCfg;
extern char g_mcDebugFooter[256];

/* основной объект класса подключения к БД */
otl_connect g_coMainDBConn;

int ConnectDB (otl_connect &p_coDBConn) {
	int iRetVal = 0;
	char mcConnStr[1024];
	std::string
		strDBUser,
		strDBPswd,
		strDBHost,
		strDBPort,
		strDBService;
	std::vector<std::string> vectValList;

	ENTER_ROUT;

	iRetVal = g_coConf.GetParamValue ("db_user", vectValList);
	if (0 == iRetVal) {
		strDBUser = vectValList[0];
	}

	vectValList.clear();
	iRetVal = g_coConf.GetParamValue ("db_pswd", vectValList);
	if (0 == iRetVal) {
		strDBPswd = vectValList[0];
	}

	vectValList.clear();
	iRetVal = g_coConf.GetParamValue ("db_host", vectValList);
	if (0 == iRetVal) {
		strDBHost = vectValList[0];
	}

	vectValList.clear();
	iRetVal = g_coConf.GetParamValue ("db_port", vectValList);
	if (0 == iRetVal) {
		strDBPort = vectValList[0];
	}

	vectValList.clear();
	iRetVal = g_coConf.GetParamValue ("db_srvc", vectValList);
	if (0 == iRetVal) {
		strDBService = vectValList[0];
	}

	snprintf(
		mcConnStr,
		sizeof(mcConnStr),
		g_mcRadDBConnTempl,
		strDBUser.c_str(),
		strDBPswd.c_str(),
		strDBHost.c_str(),
		strDBPort.c_str(),
		strDBService.c_str());
	try {
		p_coDBConn.rlogon (mcConnStr);
		g_coLog.WriteLog ("DB connected successfully");
	} catch (otl_exception &coOtlExc) {
		g_coLog.WriteLog ("ConnectDB: error: code: '%d'; description: '%s'", coOtlExc.code, coOtlExc.msg);
		iRetVal = coOtlExc.code;
	}

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

int CreateNASList (std::map<std::string,std::string> *p_pmapNASList) {
	int iRetVal = 0;
	int iFnRes;
	const char *pszConfParam = "qr_nas_list";
	std::string strVal;

	ENTER_ROUT;

	do {
		iRetVal = g_coConf.GetParamValue (pszConfParam, strVal);
		if (iRetVal) {
			g_coLog.WriteLog ("CreateNASList: Config parameter '%s' not found", pszConfParam);
			return -1;
		}
		if (0 == strVal.length()) {
			g_coLog.WriteLog ("CreateNASList: Config parameter '%s' not defined", pszConfParam);
			return -1;
		}
		try {
			otl_stream coOTLStream(
				1000,
				strVal.c_str(),
				g_coMainDBConn);
			char mcNASName[128];
			char mcLocation[128];
			while (! coOTLStream.eof()) {
				coOTLStream
					>> mcNASName
					>> mcLocation;
				p_pmapNASList->insert(
					std::make_pair(
						mcNASName,
						mcLocation));
				if (2 <= g_soMainCfg.m_iDebug) {
					g_coLog.WriteLog ("%sNASName: '%s'; Location: '%s'", FOOT(g_soMainCfg.m_iDCD), mcNASName, mcLocation);
				}
			}
		} catch (otl_exception &coOTLExc) {
			/* во время исполнения запроса произошла ошибка */
			g_coLog.WriteLog ("CreateNASList: error: query: '%s'; description: '%s'", strVal.c_str(), coOTLExc.msg);
			iRetVal = coOTLExc.code;
		}
	} while (0);

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

int CreateSubscriberList (std::vector<SSubscriberRefresh> *p_pvectRefresh) {
	int iRetVal = 0;
	int iFnRes;

	ENTER_ROUT;

	do {
		int iRepCnt = 0;
		std::string strVal;
		const char* pcszConfParam = "qr_refresh_list";
		/* запрашиваем параметр конфигурациии */
		iRetVal = g_coConf.GetParamValue (pcszConfParam, strVal);
		if (iRetVal) {
			g_coLog.WriteLog ("CreateSubscriberList: Config parameter '%s' not found", pcszConfParam);
			iRetVal = -1;
			break;
		}
		/* если значение параметра пустое */
		if (0 == strVal.length()) {
			g_coLog.WriteLog ("CreateSubscriberList: Config parameter '%s' not defined", pcszConfParam);
			iRetVal = -1;
			break;
		}

		repeat_query:
		try {
			SSubscriberRefresh soSubscr;
			/* готовим запрос запрос к БД */
			otl_stream coOTLStream (1000, strVal.c_str(), g_coMainDBConn);
			while (! coOTLStream.eof()) {
				memset (&soSubscr, 0, sizeof(soSubscr));
				coOTLStream
					>> soSubscr.m_mcSubscriberId
					>> soSubscr.m_mcRefreshDate
					>> soSubscr.m_mcAction;
				p_pvectRefresh->push_back (soSubscr);
				if (2 <= g_soMainCfg.m_iDebug) {
					g_coLog.WriteLog ("%sSubscriber_Id: '%s'; Refresh_Date: '%s'; action: '%s'", FOOT(g_soMainCfg.m_iDCD), soSubscr.m_mcSubscriberId, soSubscr.m_mcRefreshDate, soSubscr.m_mcAction);
				}
			}
		} catch (otl_exception &coOTLExc) {
			/* выполняем только один раз */
			if (0 == iRepCnt++) {
				/* проверяем работоспособность соединения с БД */
				iFnRes = CheckDBConnection (g_coMainDBConn);
				/* если соединение потеряно пытаемся его восстановить */
				if (iFnRes) {
					g_coLog.WriteLog ("CreateSubscriberList: CheckDBConnection: error: code: '%d'", iFnRes);
					iFnRes = ReconnectDB (g_coMainDBConn);
					/* если соединение удалось восстановить */
					if (0 == iFnRes) {
						goto repeat_query;
					} else {
						g_coLog.WriteLog ("CreateSubscriberList: ReconnectDB: error: code: '%d'", iFnRes);
						break;
					}
				}
			}
			/* возникла ошибка выполнения запроса */
			g_coLog.WriteLog ("CreateSubscriberList: error: query '%s'; description: '%s'", strVal.c_str(), coOTLExc.msg);
			iRetVal = coOTLExc.code;
		}
	} while (0);

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

int CreateSessionList (const char *p_pcszSubscriberID, std::map<SSessionInfo,std::map<std::string,int> > *p_pmapSessList, otl_connect &p_coDBConn)
{
	int iRetVal = 0;
	int iFnRes;
	const char *pszConfParam = "qr_session_list";
	std::string strRequest;
	int iRepCnt = 0;

	ENTER_ROUT;

	do {
		/* запрашиваем в конфиге текст запроса */
		iRetVal = g_coConf.GetParamValue (pszConfParam, strRequest);
		if (iRetVal) {
			g_coLog.WriteLog ("CreateSessionList: error: Config parameter '%s' not found", pszConfParam);
			iRetVal -1;
			break;
		}
		/* если текст запроса пуст */
		if (0 == strRequest.length()) {
			g_coLog.WriteLog ("CreateSessionList: error: Config parameter '%s' not defined", pszConfParam);
			iRetVal -1;
			break;
		}

		std::map<SSessionInfo,std::map<std::string,int> >::iterator iterSessInfo;

		repeat_query:
		try {
			if (2 <= g_soMainCfg.m_iDebug) {
				g_coLog.WriteLog ("%sCreateSessionList: DB query started", FOOT(g_soMainCfg.m_iDCD));
			}
			char mcCiscoServiceInfo[128];
			SSessionInfo soSessInfo;
			otl_stream coOTLStream (1000, strRequest.c_str(), p_coDBConn);

			coOTLStream << p_pcszSubscriberID;
			while (! coOTLStream.eof()) {
				memset (&soSessInfo, 0, sizeof(soSessInfo));
				coOTLStream
					>> soSessInfo.m_mcUserName
					>> soSessInfo.m_mcNasIPAddress
					>> soSessInfo.m_mcCiscoParentSessionId
					>> mcCiscoServiceInfo;
				iFnRes = GetNASLocation (soSessInfo.m_mcNasIPAddress, soSessInfo.m_mcLocation, sizeof(soSessInfo.m_mcLocation));
				if (iFnRes) {
					continue;
				}
				if (0 == strlen (soSessInfo.m_mcLocation)) {
					strcpy (soSessInfo.m_mcLocation, "DEFAULT");
				}
				ModifyName ("sess_info_pref", soSessInfo.m_mcLocation, mcCiscoServiceInfo);
				iterSessInfo = p_pmapSessList->find (soSessInfo);
				if (iterSessInfo != p_pmapSessList->end()) {
					iterSessInfo->second.insert(
						std::make_pair(
							mcCiscoServiceInfo,
							0));
				} else {
					std::map<std::string,int> mapTmp;
					mapTmp.insert(
						std::make_pair(
							mcCiscoServiceInfo,
							0));
					p_pmapSessList->insert(
						std::make_pair(
							soSessInfo,
							mapTmp));
				}
				if (2 <= g_soMainCfg.m_iDebug) {
					g_coLog.WriteLog(
						"%sUserName: '%s'; NasIPAddress: '%s'; CiscoParentSessionId: '%s'; CiscoServiceInfo: '%s'",
						FOOT (g_soMainCfg.m_iDCD),
						soSessInfo.m_mcUserName, soSessInfo.m_mcNasIPAddress, soSessInfo.m_mcCiscoParentSessionId, mcCiscoServiceInfo);
				}
			}
		} catch (otl_exception &coOTLExc) {
			/* выполняем только один раз */
			if (0 == iRepCnt++) {
				/* проверяем работоспособность соединения с БД */
				iFnRes = CheckDBConnection (p_coDBConn);
				/* если соединение потеряно пытаемся его восстановить */
				if (iFnRes) {
					g_coLog.WriteLog ("CreateSessionList: CheckDBConnection: error: code: '%d'", iFnRes);
					iFnRes = ReconnectDB (p_coDBConn);
					/* если соединение удалось восстановить */
					if (0 == iFnRes) {
						goto repeat_query;
					} else {
						g_coLog.WriteLog ("CreateSessionList: ReconnectDB: error: code: '%d'", iFnRes);
						break;
					}
				}
			}
			/* во время выполнения запроса произошла ошибка */
			g_coLog.WriteLog ("CreateSessionList: error: query: '%s'; code: '%d'; description: '%s'", strRequest.c_str(), coOTLExc.code, coOTLExc.msg);
			iRetVal = coOTLExc.code;
		}
		if (2 <= g_soMainCfg.m_iDebug) {
			g_coLog.WriteLog ("%sCreateSessionList: DB query finished", FOOT(g_soMainCfg.m_iDCD));
		}
	} while (0);

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

int CreatePolicyList (const SSessionInfo *p_pcsoSessInfo, std::map<SPolicyInfo,std::map<SPolicyDetail,int> > *p_pmapPolicy, otl_connect &p_coDBConn)
{
	int iRetVal = 0;
	int iFnRes;
	int iRepCnt = 0;
	std::string strRequest;
	const char *pcszConfParam;

	ENTER_ROUT;

	do {
		pcszConfParam = "qr_policy_list";
		/* запрашиваем текст запроса в конфиге */
		iRetVal = g_coConf.GetParamValue (pcszConfParam, strRequest);
		if (iRetVal) {
			g_coLog.WriteLog ("CreatePolicyList: error: Config parameter '%s' not found", pcszConfParam);
			break;
		}
		/* если текст запроса пустой */
		if (0 == strRequest.length()) {
			g_coLog.WriteLog ("CreatePolicyList: error: Config parameter '%s' not defined", pcszConfParam);
			iRetVal = -1;
			break;
		}

		std::map<SPolicyInfo,std::map<SPolicyDetail,int> >::iterator iterPolicy;

		repeat_query:
		try {
			SPolicyInfo soPolicyInfo;
			SPolicyDetail soPolicyDetail;
			otl_stream coOTLStream (1000, strRequest.c_str(), p_coDBConn);
			coOTLStream
				<< p_pcsoSessInfo->m_mcUserName
				<< p_pcsoSessInfo->m_mcLocation;
			strcpy (soPolicyInfo.m_mcUserName, p_pcsoSessInfo->m_mcUserName);
			while (! coOTLStream.eof()) {
				coOTLStream
					>> soPolicyInfo.m_mcLocation
					>> soPolicyDetail.m_mcAttr
					>> soPolicyDetail.m_mcValue;
				if (2 <= g_soMainCfg.m_iDebug) {
					g_coLog.WriteLog ("%sUserName: '%s'; Location: '%s'; Attribute: '%s'; Value: '%s'", FOOT(g_soMainCfg.m_iDCD), soPolicyInfo.m_mcUserName, soPolicyInfo.m_mcLocation, soPolicyDetail.m_mcAttr, soPolicyDetail.m_mcValue);
				}
				if (0 == strlen (soPolicyInfo.m_mcLocation)) {
					if (2 <= g_soMainCfg.m_iDebug) {
						g_coLog.WriteLog ("%sPolicy location not defined. 'DEFAULT' location applied", FOOT(g_soMainCfg.m_iDCD));
					}
					strcpy (soPolicyInfo.m_mcLocation, "DEFAULT");
				}
				if (! Filter ("policy_filter", soPolicyInfo.m_mcLocation, soPolicyDetail.m_mcAttr)) {
					continue;
				}
				ModifyName ("policy_pref", soPolicyInfo.m_mcLocation, soPolicyDetail.m_mcValue);
				iterPolicy = p_pmapPolicy->find (soPolicyInfo);
				if (iterPolicy != p_pmapPolicy->end()) {
					iterPolicy->second.insert(
						std::make_pair(
							soPolicyDetail,
							0));
				}
				else {
					std::map<SPolicyDetail,int> mapTmp;
					mapTmp.insert(
						std::make_pair(
							soPolicyDetail,
							0));
					p_pmapPolicy->insert(
						std::make_pair(
							soPolicyInfo,
							mapTmp));
				}
			}
		} catch (otl_exception &coOTLExc) {
			/* выполняем только один раз */
			if (0 == iRepCnt++) {
				/* проверяем работоспособность соединения с БД */
				iFnRes = CheckDBConnection (p_coDBConn);
				/* если соединение потеряно пытаемся его восстановить */
				if (iFnRes) {
					g_coLog.WriteLog ("CreatePolicyList: CheckDBConnection: error: code: '%d'", iFnRes);
					iFnRes = ReconnectDB (p_coDBConn);
					/* если соединение удалось восстановить */
					if (0 == iFnRes) {
						goto repeat_query;
					} else {
						g_coLog.WriteLog ("CreatePolicyList: ReconnectDB: error: code: '%d'", iFnRes);
						break;
					}
				}
			}
			/* во время выполенния запроса произошла ошибка */
			g_coLog.WriteLog ("CreatePolicyList: error: query: '%s'; description: '%s'", strRequest.c_str(), coOTLExc.msg);
			iRetVal = coOTLExc.code;
		}
	} while (0);

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

int DeleteRefreshRecord (const SSubscriberRefresh *p_pcsoSubscr, otl_connect &p_coDBConn)
{
	int iRetVal = 0;
	int iFnRes;
	int iRepCnt = 0;
	const char *pszConfParam = "qr_delete_refresh_row";
	std::string strRequest;

	ENTER_ROUT;

	do {
		/* запрашиваем в конфиге текст запроса */
		iRetVal = g_coConf.GetParamValue (pszConfParam, strRequest);
		if (iRetVal) {
			g_coLog.WriteLog ("DeleteRefreshRecord: error: Config parameter '%s' not found", pszConfParam);
			break;
		}
		/* если текст запроса пустой */
		if (0 == strRequest.length()) {
			g_coLog.WriteLog ("DeleteRefreshRecord: error: Config parameter '%s' not defined", pszConfParam);
			iRetVal -1;
			break;
		}

		repeat_query:
		try {
			otl_stream coOTLStream (1, strRequest.c_str(), p_coDBConn);
			coOTLStream
				<< p_pcsoSubscr->m_mcSubscriberId
				<< p_pcsoSubscr->m_mcRefreshDate;
			coOTLStream.flush ();
			p_coDBConn.commit();
			if (2 <= g_soMainCfg.m_iDebug) {
				g_coLog.WriteLog ("%sDeleted record: SubscreberId: '%s'; RefreshDate: '%s'", FOOT(g_soMainCfg.m_iDCD), p_pcsoSubscr->m_mcSubscriberId, p_pcsoSubscr->m_mcRefreshDate);
			}
		} catch (otl_exception &coOTLExc) {
			/* выполняем только один раз */
			if (0 == iRepCnt++) {
				/* проверяем работоспособность соединения с БД */
				iFnRes = CheckDBConnection (p_coDBConn);
				/* если соединение потеряно пытаемся его восстановить */
				if (iFnRes) {
					g_coLog.WriteLog ("DeleteRefreshRecord: CheckDBConnection: error: code: '%d'", iFnRes);
					iFnRes = ReconnectDB (p_coDBConn);
					/* если соединение удалось восстановить */
					if (0 == iFnRes) {
						goto repeat_query;
					} else {
						g_coLog.WriteLog ("DeleteRefreshRecord: ReconnectDB: error: code: '%d'", iFnRes);
						break;
					}
				}
			}
			/* во время выполнения запроса произошла ошибка */
			g_coLog.WriteLog ("DeleteRefreshRecord: error: query: '%s'; description: '%s'", strRequest.c_str(), coOTLExc.msg);
			iRetVal = coOTLExc.code;
		}

	} while (0);

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

int FixStuckSession (const SSessionInfo *p_pcsoSessInfo, otl_connect &p_coDBConn, bool p_bOpt)
{
	int iRetVal = 0;
	int iFnRes;
	int iRepCnt = 0;
	const char *pszConfParam;
	std::string strRequest;

	ENTER_ROUT;

	if (p_bOpt) {
		pszConfParam = "qr_fix_stuck_session_opt";
	} else {
		pszConfParam = "qr_fix_stuck_session";
	}

	do {
		/* запрашиваем в конфиге текст запроса */
		iRetVal = g_coConf.GetParamValue (pszConfParam, strRequest);
		if (iRetVal) {
			/* опциональный запрос не обязательно должен быть описан в конфиге */
			if (p_bOpt) {
				iRetVal = 0;
				break;
			}
			g_coLog.WriteLog ("FixStuckSession: error: Config parameter '%s' not found", pszConfParam);
			break;
		}
		/* если текст запроса пустой */
		if (0 == strRequest.length()) {
			/* опциональный запрос не обязательно должен быть описан в конфиге */
			if (p_bOpt) {
				iRetVal = 0;
				break;
			}
			g_coLog.WriteLog ("FixStuckSession: error: Config parameter '%s' not defined", pszConfParam);
			iRetVal -1;
			break;
		}

		repeat_query:
		try {
			otl_stream coOTLStream (1, strRequest.c_str(), p_coDBConn);
			coOTLStream
				<< p_pcsoSessInfo->m_mcUserName
				<< p_pcsoSessInfo->m_mcNasIPAddress
				<< p_pcsoSessInfo->m_mcCiscoParentSessionId;
			coOTLStream.flush ();
			p_coDBConn.commit();
		} catch (otl_exception &coOTLExc) {
			/* выполняем только один раз */
			if (0 == iRepCnt++) {
				/* проверяем работоспособность соединения с БД */
				iFnRes = CheckDBConnection (p_coDBConn);
				/* если соединение потеряно пытаемся его восстановить */
				if (iFnRes) {
					g_coLog.WriteLog ("FixStuckSession: CheckDBConnection: error: code: '%d'", iFnRes);
					iFnRes = ReconnectDB (p_coDBConn);
					/* если соединение удалось восстановить */
					if (0 == iFnRes) {
						goto repeat_query;
					} else {
						g_coLog.WriteLog ("FixStuckSession: ReconnectDB: error: code: '%d'", iFnRes);
						break;
					}
				}
			}
			/* во время выполнения запроса произошла ошибка */
			g_coLog.WriteLog ("FixStuckSession: error: query: '%s'; description: '%s'", strRequest.c_str(), coOTLExc.msg);
			iRetVal = coOTLExc.code;
		}
	} while (0);

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

int ReconnectDB (otl_connect &p_coDBConn)
{
	int iRetVal = 0;
	int iFnRes;

	ENTER_ROUT;

	do {
		if (p_coDBConn.connected) {
			DisconnectDB (p_coDBConn);
		}
		g_coLog.WriteLog ("ReconnectDB: info: trying to reconnect");
		iFnRes = ConnectDB (p_coDBConn);
		if (iFnRes) {
			/* подключиться к БД не удалось */
			g_coLog.WriteLog ("ReconnectDB: error: reconnect failed");
			iRetVal = -200001;
			break;
		}
	} while (0);

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

int CheckDBConnection (otl_connect &p_coDBConn)
{
	int iRetVal = 0;

	ENTER_ROUT;

	/* объект класса не подключен к БД */
	if (! p_coDBConn.connected) {
		return -200002;
	}

	/* проверяем работоспособность подключения на простейшем запросе */
	try {
		char mcTime[128];
		otl_stream coStream (1, "select to_char(sysdate, 'yyyy') from dual", p_coDBConn);
		coStream >> mcTime;
	} catch (otl_exception &coExc) {
		/* если запрос выполнился с ошибкой */
		g_coLog.WriteLog ("CheckDBConnection: connection test failed: error: '%s'", coExc.msg);
		iRetVal = -200003;
	}

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

void DisconnectDB (otl_connect &p_coDBConn)
{
	ENTER_ROUT;

	p_coDBConn.logoff ();

	LEAVE_ROUT (0);
}
