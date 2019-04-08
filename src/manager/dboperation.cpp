#include "utils/config/config.h"
#include "utils/log/log.h"

#include "coam.h"

extern CConfig g_coConf;
extern CLog g_coLog;
extern SMainConfig g_soMainCfg;
extern char g_mcDebugFooter[256];

int CreateNASList (std::map<std::string,std::string> *p_pmapNASList)
{
	int iRetVal = 0;
	const char *pszConfParam = "qr_nas_list";
	std::string strVal;
	otl_connect *pcoDBConn = NULL;

	do {
		pcoDBConn = db_pool_get();
		if (NULL == pcoDBConn) {
			UTL_LOG_E(g_coLog, "can not to get free DB connection");
			iRetVal = -1;
			break;
		}
		iRetVal = g_coConf.GetParamValue (pszConfParam, strVal);
		if (iRetVal) {
			UTL_LOG_E(g_coLog, "config parameter '%s' not found", pszConfParam);
			break;
		}
		if (0 == strVal.length()) {
			UTL_LOG_E(g_coLog, "config parameter '%s' not defined", pszConfParam);
			break;
		}
		try {
			otl_stream coOTLStream(
				1000,
				strVal.c_str(),
				*pcoDBConn);
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
			}
		} catch (otl_exception &coOTLExc) {
			/* �� ����� ���������� ������� ��������� ������ */
			UTL_LOG_E(g_coLog, "code: '%d; message: '%s'; query: '%s'", coOTLExc.code, coOTLExc.msg, coOTLExc.stm_text);
			iRetVal = coOTLExc.code;
		}
	} while (0);

	if (pcoDBConn)
		db_pool_release(pcoDBConn);

	return iRetVal;
}

int CreateSubscriberList (std::vector<SSubscriberRefresh> *p_pvectRefresh)
{
	int iRetVal = 0;
	otl_connect *pcoDBConn = NULL;

	do {
		pcoDBConn = db_pool_get();
		if (NULL == pcoDBConn) {
			UTL_LOG_E(g_coLog, "can not to get free DB connection");
			break;
		}
		std::string strVal;
		const char* pcszConfParam = "qr_refresh_list";
		/* ����������� �������� ������������� */
		iRetVal = g_coConf.GetParamValue (pcszConfParam, strVal);
		if (iRetVal) {
			UTL_LOG_E(g_coLog, "config parameter '%s' not found", pcszConfParam);
			iRetVal = -1;
			break;
		}
		/* ���� �������� ��������� ������ */
		if (0 == strVal.length()) {
			UTL_LOG_E(g_coLog, "config parameter '%s' not defined", pcszConfParam);
			iRetVal = -1;
			break;
		}

		try {
			SSubscriberRefresh soSubscr;
			/* ������� ������ ������ � �� */
			otl_stream coOTLStream (1000, strVal.c_str(), *pcoDBConn);
			while (! coOTLStream.eof()) {
				memset (&soSubscr, 0, sizeof(soSubscr));
				coOTLStream
					>> soSubscr.m_mcSubscriberId
					>> soSubscr.m_mcRefreshDate
					>> soSubscr.m_mcAction;
				p_pvectRefresh->push_back (soSubscr);
			}
		} catch (otl_exception &coOTLExc) {
			/* �������� ������ ���������� ������� */
			UTL_LOG_E(g_coLog, "code: '%d'; message: '%s'; query '%s';", coOTLExc.code, coOTLExc.msg, coOTLExc.stm_text);
			iRetVal = coOTLExc.code;
		}
	} while (0);

	if (pcoDBConn) {
		db_pool_release(pcoDBConn);
	}

	return iRetVal;
}

int OperateSessionInfo(
	std::map<SSessionInfo, std::map<std::string, int> > *p_pmapSessList,
	SSessionInfo &p_soSessInfo,
	std::string &p_strServiceInfo)
{
	int iRetVal = 0;
	int iFnRes;

	do {
		std::map<SSessionInfo, std::map<std::string, int> >::iterator iterSessInfo;

		iFnRes = GetNASLocation(p_soSessInfo.m_strNASIPAddress, p_soSessInfo.m_strLocation);
		/* ���� NAS �� ������ */
		if (iFnRes) {
			p_soSessInfo.m_strLocation = "DEFAULT";
		}
		ModifyName("sess_info_pref", p_soSessInfo.m_strLocation, p_strServiceInfo); /* it should be deprecated */
		ModifyName("activeServiceName_modifier", p_soSessInfo.m_strLocation, p_strServiceInfo);
		iterSessInfo = p_pmapSessList->find(p_soSessInfo);
		if (iterSessInfo != p_pmapSessList->end()) {
			iterSessInfo->second.insert(
				std::make_pair(
				p_strServiceInfo,
				0));
		} else {
			std::map<std::string, int> mapTmp;
			mapTmp.insert(
				std::make_pair(
				p_strServiceInfo,
				0));
			p_pmapSessList->insert(
				std::make_pair(
				p_soSessInfo,
				mapTmp));
		}
	} while (0);

	return iRetVal;
}

extern std::multimap<std::string, SSessionInfoFull> g_mmapSessionListFull;

int CreateSessionList (
	const char *p_pcszSubscriberID,
	std::map<SSessionInfo,std::map<std::string,int> > *p_pmapSessList,
	otl_connect &p_coDBConn)
{
	int iRetVal = 0;
	const char *pszConfParam = "qr_session_list";
	std::string strRequest;

	do {
		if (g_mmapSessionListFull.size() > 0) {
			for (std::multimap<std::string, SSessionInfoFull>::iterator iterSessInfoFull = g_mmapSessionListFull.find(p_pcszSubscriberID);
					iterSessInfoFull != g_mmapSessionListFull.end() && iterSessInfoFull->first == p_pcszSubscriberID;
					++iterSessInfoFull) {
				OperateSessionInfo(p_pmapSessList, iterSessInfoFull->second.m_soSessInfo, iterSessInfoFull->second.m_strServiceInfo);
			}
		} else {
			/* ����������� � ������� ����� ������� */
			iRetVal = g_coConf.GetParamValue (pszConfParam, strRequest);
			if (iRetVal) {
				UTL_LOG_E(g_coLog, "Config parameter '%s' not found", pszConfParam);
				iRetVal = -1;
				break;
			}
			/* ���� ����� ������� ���� */
			if (0 == strRequest.length()) {
				UTL_LOG_E(g_coLog, "Config parameter '%s' not defined", pszConfParam);
				iRetVal = -1;
				break;
			}

			try {
				std::string strCiscoServiceInfo;
				SSessionInfo soSessInfo;
				otl_stream coOTLStream (1000, strRequest.c_str(), p_coDBConn);

				coOTLStream << p_pcszSubscriberID;
				while (! coOTLStream.eof()) {
					coOTLStream
						>> soSessInfo.m_strUserName
						>> soSessInfo.m_strNASIPAddress
						>> soSessInfo.m_strSessionId
						>> strCiscoServiceInfo;
					OperateSessionInfo(p_pmapSessList, soSessInfo, strCiscoServiceInfo);
				}
			} catch (otl_exception &coOTLExc) {
				/* �� ����� ���������� ������� ��������� ������ */
					UTL_LOG_E(g_coLog, "code: '%d'; description: '%s'; query: '%s';", coOTLExc.code, coOTLExc.msg, coOTLExc.stm_text);
				iRetVal = coOTLExc.code;
			}
		}
	} while (0);

	return iRetVal;
}

int CreateSessionListFull(std::multimap<std::string, SSessionInfoFull> &p_mmapSessList)
{
	int iRetVal = 0;
	const char *pszConfParam = "qr_session_list_full";
	std::string strRequest;
	otl_connect *pcoDBConn = NULL;
	CTimeMeasurer coTM;

	do {
		pcoDBConn = db_pool_get();
		if (NULL == pcoDBConn) {
			iRetVal = -1;
			UTL_LOG_F(g_coLog, "can not to get DB connection");
			break;
		}
		/* ����������� � ������� ����� ������� */
		iRetVal = g_coConf.GetParamValue(pszConfParam, strRequest);
		if (iRetVal) {
			UTL_LOG_E(g_coLog, "Config parameter '%s' not found", pszConfParam);
			iRetVal = -2;
			break;
		}
		/* ���� ����� ������� ���� */
		if (0 == strRequest.length()) {
			UTL_LOG_E(g_coLog, "Config parameter '%s' not defined", pszConfParam);
			iRetVal = -3;
			break;
		}

		try {
			std::string strSubscriberId;
			otl_stream coOTLStream(
				10000,
				strRequest.c_str(),
				*pcoDBConn);

			while (!coOTLStream.eof()) {
				{
					SSessionInfoFull soSessInfoFull;
					coOTLStream
						>> strSubscriberId
						>> soSessInfoFull.m_soSessInfo.m_strUserName
						>> soSessInfoFull.m_soSessInfo.m_strNASIPAddress
						>> soSessInfoFull.m_soSessInfo.m_strSessionId
						>> soSessInfoFull.m_strServiceInfo;
					p_mmapSessList.insert(std::make_pair(strSubscriberId, soSessInfoFull));
				}
			}
		} catch (otl_exception &coOTLExc) {
			/* �� ����� ���������� ������� ��������� ������ */
			UTL_LOG_E(g_coLog, "code: '%d'; description: '%s'; query: '%s';", coOTLExc.code, coOTLExc.msg, coOTLExc.stm_text);
			iRetVal = coOTLExc.code;
		}
	} while (0);

	if (pcoDBConn)
		db_pool_release(pcoDBConn);

	if (0 == iRetVal) {
		char mcTimeInterval[256];
		coTM.GetDifference(NULL, mcTimeInterval, sizeof(mcTimeInterval));
		UTL_LOG_N(g_coLog, "session list fetched in '%s'; row count: '%d'", mcTimeInterval, g_mmapSessionListFull.size());
	}

	return iRetVal;
}

int CreatePolicyList (
	const SSessionInfo *p_pcsoSessInfo,
	std::map<SPolicyInfo,std::map<SPolicyDetail,int> > *p_pmapPolicy,
	otl_connect &p_coDBConn)
{
	int iRetVal = 0;
	std::string strRequest;
	const char *pcszConfParam;

	do {
		pcszConfParam = "qr_policy_list";
		/* ����������� ����� ������� � ������� */
		iRetVal = g_coConf.GetParamValue (pcszConfParam, strRequest);
		if (iRetVal) {
			UTL_LOG_E(g_coLog, "config parameter '%s' not found", pcszConfParam);
			break;
		}
		/* ���� ����� ������� ������ */
		if (0 == strRequest.length()) {
			UTL_LOG_E(g_coLog, "config parameter '%s' not defined", pcszConfParam);
			iRetVal = -1;
			break;
		}

		std::map<SPolicyInfo,std::map<SPolicyDetail,int> >::iterator iterPolicy;

		try {
			SPolicyInfo soPolicyInfo;
			SPolicyDetail soPolicyDetail;
			otl_stream coOTLStream (1000, strRequest.c_str(), p_coDBConn);
			coOTLStream
				<< p_pcsoSessInfo->m_strUserName
				<< p_pcsoSessInfo->m_strLocation;
			soPolicyInfo.m_strUserName = p_pcsoSessInfo->m_strUserName;
			while (! coOTLStream.eof()) {
				coOTLStream
					>> soPolicyInfo.m_strLocation
					>> soPolicyDetail.m_strAttr
					>> soPolicyDetail.m_strValue;
				if (0 == soPolicyInfo.m_strLocation.length()) {
					soPolicyInfo.m_strLocation = "DEFAULT";
				}
				if (! Filter ("policy_filter", soPolicyInfo.m_strLocation, soPolicyDetail.m_strAttr)) { /* it should be deprecated */
					UTL_LOG_N( g_coLog, "attribute was declined by policy_filter: location: %s; attr: %s", soPolicyInfo.m_strLocation.c_str(), soPolicyDetail.m_strAttr.c_str() );
					continue;
				}
				if (! Filter ("policyAttr_filter", soPolicyInfo.m_strLocation, soPolicyDetail.m_strAttr)) {
					UTL_LOG_N( g_coLog, "attribute was declined by policyAttr_filter: location: %s; attr: %s", soPolicyInfo.m_strLocation.c_str(), soPolicyDetail.m_strAttr.c_str() );
					continue;
				}
				if( ! Filter( "policyName_filter", soPolicyInfo.m_strLocation, soPolicyDetail.m_strValue ) ) {
					UTL_LOG_N( g_coLog, "attribute was declined by policyName_filter: location: %s; attr: %s", soPolicyInfo.m_strLocation.c_str(), soPolicyDetail.m_strValue.c_str() );
					continue;
				}
				ModifyName ("policy_pref", soPolicyInfo.m_strLocation, soPolicyDetail.m_strValue); /* it should be deprecated */
				ModifyName( "policyName_modifier", soPolicyInfo.m_strLocation, soPolicyDetail.m_strValue );
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
			/* �� ����� ���������� ������� ��������� ������ */
			UTL_LOG_E(g_coLog, "code: '%d'; message: '%s'; query: '%s';", coOTLExc.code, coOTLExc.msg, coOTLExc.stm_text);
			iRetVal = coOTLExc.code;
		}
	} while (0);

	return iRetVal;
}

int DeleteRefreshRecord (
	const SSubscriberRefresh *p_pcsoSubscr,
	otl_connect &p_coDBConn)
{
	int iRetVal = 0;
	const char *pszConfParam = "qr_delete_refresh_row";
	std::string strRequest;

	do {
		/* ����������� � ������� ����� ������� */
		iRetVal = g_coConf.GetParamValue (pszConfParam, strRequest);
		if (iRetVal) {
			UTL_LOG_E(g_coLog, "Config parameter '%s' not found", pszConfParam);
			break;
		}
		/* ���� ����� ������� ������ */
		if (0 == strRequest.length()) {
			UTL_LOG_E(g_coLog, "Config parameter '%s' not defined", pszConfParam);
			iRetVal = -1;
			break;
		}

		try {
			otl_stream coOTLStream (1, strRequest.c_str(), p_coDBConn);
			coOTLStream
				<< p_pcsoSubscr->m_mcSubscriberId
				<< p_pcsoSubscr->m_mcRefreshDate;
			coOTLStream.flush ();
			p_coDBConn.commit();
		} catch (otl_exception &coOTLExc) {
			/* �� ����� ���������� ������� ��������� ������ */
			UTL_LOG_E(g_coLog, "code: '%d'; message: '%s'; query: '%s'", coOTLExc.code, coOTLExc.msg, coOTLExc.stm_text);
			iRetVal = coOTLExc.code;
		}

	} while (0);

	return iRetVal;
}

int FixStuckSession (
	const SSessionInfo *p_pcsoSessInfo,
	otl_connect &p_coDBConn,
	bool p_bOpt)
{
	int iRetVal = 0;
	const char *pszConfParam;
	std::string strRequest;

	if (p_bOpt) {
		pszConfParam = "qr_fix_stuck_session_opt";
	} else {
		pszConfParam = "qr_fix_stuck_session";
	}

	do {
		/* ����������� � ������� ����� ������� */
		iRetVal = g_coConf.GetParamValue (pszConfParam, strRequest);
		if (iRetVal) {
			/* ������������ ������ �� ����������� ������ ���� ������ � ������� */
			if (p_bOpt) {
				iRetVal = 0;
				break;
			}
			UTL_LOG_E(g_coLog, "config parameter '%s' not found", pszConfParam);
			break;
		}
		/* ���� ����� ������� ������ */
		if (0 == strRequest.length()) {
			/* ������������ ������ �� ����������� ������ ���� ������ � ������� */
			if (p_bOpt) {
				iRetVal = 0;
				break;
			}
			UTL_LOG_E(g_coLog, "config parameter '%s' not defined", pszConfParam);
			iRetVal = -1;
			break;
		}

		try {
			otl_stream coOTLStream (1, strRequest.c_str(), p_coDBConn);
			coOTLStream
				<< p_pcsoSessInfo->m_strUserName
				<< p_pcsoSessInfo->m_strNASIPAddress
				<< p_pcsoSessInfo->m_strSessionId;
			coOTLStream.flush ();
			p_coDBConn.commit();
		} catch (otl_exception &coOTLExc) {
			/* �� ����� ���������� ������� ��������� ������ */
			UTL_LOG_E(g_coLog, "code: '%d'; message: '%s'; query: '%s'", coOTLExc.code, coOTLExc.msg, coOTLExc.stm_text);
			iRetVal = coOTLExc.code;
		}
	} while (0);

	return iRetVal;
}
