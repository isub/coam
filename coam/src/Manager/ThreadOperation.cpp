#include "../../../utils/config/Config.h"
#include "../../../utils/log/Log.h"

#include "coam.h"

#include <semaphore.h>
#include <errno.h>
#include <stdlib.h>

extern CConfig g_coConf;
extern CLog g_coLog;
extern SMainConfig g_soMainCfg;
extern char g_mcDebugFooter[256];

SThreadInfo *g_pmsoThreadInfo = NULL;
unsigned int g_uiThreadCount;
sem_t g_tSem;

int InitThreadPool ()
{
	int iRetVal = 0;
	int iFnRes;

	ENTER_ROUT;

	do {
		std::string strValue;
		const char *pcszConfParamName = "thread_count";

		/* ����������� �������� �� ������� */
		iFnRes = g_coConf.GetParamValue (pcszConfParamName, strValue);
		if (iFnRes) {
			g_coLog.WriteLog ("InitThreadPool: Config parameter '%s' not found", pcszConfParamName);
			iRetVal = -1000;
			break;
		}
		if (0 == strValue.length ()) {
			g_coLog.WriteLog ("InitThreadPool: Config parameter '%s' not defined", pcszConfParamName);
			iRetVal = -1010;
			break;
		}
		/* ����������� ������ � ����� */
		if (g_soMainCfg.m_iDebug > 0) {
			g_uiThreadCount = 1;
		} else {
			g_uiThreadCount = atol (strValue.c_str ());
		}
		if (0 == g_uiThreadCount) {
			g_coLog.WriteLog ("InitThreadPool: error: invalid '%s' value: '%s'", pcszConfParamName, strValue.c_str ());
			iRetVal = -1020;
			break;
		}
		/* �������������� ������� */
		iFnRes = sem_init (&g_tSem, 0, g_uiThreadCount);
		if (iFnRes) {
			char mcError[0x400];
			iRetVal = errno;
			strerror_r (iRetVal, mcError, sizeof (mcError));
			g_coLog.WriteLog ("InitThreadPool: sem_init: error: code '%d'; description: '%s'", iRetVal, mcError);
			iRetVal = -1030;
			break;
		}
		/* �������� ������ */
		g_pmsoThreadInfo = new SThreadInfo [g_uiThreadCount];
		if (NULL == g_pmsoThreadInfo) {
			/* ������ �� �������� */
			g_coLog.WriteLog ("InitThreadPool: can not allocate thread array: out of memory");
			iRetVal = -1040;
			break;
		}
		/* �������������� ������ */
		memset (g_pmsoThreadInfo, 0, sizeof (*g_pmsoThreadInfo) * g_uiThreadCount);
		/* �������� ����������� ������� � ������� ������ */
		for (unsigned int i = 0; i < g_uiThreadCount; ++ i) {
			iFnRes = InitThread (g_pmsoThreadInfo[i]);
		}
	} while (0);

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

int InitThread (SThreadInfo &p_soThreadInfo)
{
	int iRetVal = 0;
	int iFnRes;

	ENTER_ROUT;

	do {
		/* ��� �������������� ������������� � '0' ��� ���������� ������ */
		p_soThreadInfo.m_iRetVal = 0;
		/* ��������, ��� ����� ���� �� ������ */
		p_soThreadInfo.m_tThreadId = -1;
		/* ����� ��������� ��������� */
		p_soThreadInfo.m_iBusy = 0;
		/* � ������� ���������� ������ */
		p_soThreadInfo.m_iExit = 0;
		/* �������������� ������� */
		iFnRes = pthread_mutex_init (&(p_soThreadInfo.m_tMutex), NULL);
		if (iFnRes) {
			g_coLog.WriteLog ("InitThread: pthread_mutex_init error: '%d'", iFnRes);
			iRetVal = -4000;
			break;
		}
		/* �������������� ����� ���������, ���������� ���������� � ������� ������� ���������� ������� */
		memset (&p_soThreadInfo.m_soSubscriberRefresh, 0, sizeof (p_soThreadInfo.m_soSubscriberRefresh));
		/* ������� ����������� � CoASensd */
		p_soThreadInfo.m_pcoIPConn = new CIPConnector (10);
		/* ������� ����������� �� */
		p_soThreadInfo.m_pcoDBConn = new otl_connect;
		iFnRes = ConnectDB (*(p_soThreadInfo.m_pcoDBConn));
		/* ���� �������� ������ ��� ����������� � �� */
		if (iFnRes) {
			iRetVal = 4010;
			break;
		}
		/* ����� ������������� �� ��� ����� � �������� ��������� �� ������ ������ �������� ��� ������� */
		pthread_mutex_trylock (&(p_soThreadInfo.m_tMutex));
		/* ������ ������ */
		iFnRes = pthread_create (&(p_soThreadInfo.m_tThreadId), NULL, ThreadWorker, &(p_soThreadInfo));
		if (iFnRes) {
			/* ��������, ��� ����� �� ������ */
			p_soThreadInfo.m_tThreadId = -1;
			iRetVal = -4020;
			break;
		}
	} while (0);

	/* ���� �������� ������ ������������� ������*/
	if (iRetVal) {
		/* ����������� ������� ������� */
		CleanUpThreadInfo (&p_soThreadInfo);
	}

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

void DeInitThreadPool ()
{
	ENTER_ROUT;

	/* ���������� ���������� ������ ������� */
	for (unsigned int i = 0; i < g_uiThreadCount; ++ i) {
		/* ������ ������� ���������� ������ ������ */
		g_pmsoThreadInfo[i].m_iExit = 1;
		/* ������������ ������� */
		pthread_mutex_unlock (&(g_pmsoThreadInfo[i].m_tMutex));
	}
	/* ������� ���������� ������� */
	for (unsigned int i = 0; i < g_uiThreadCount; ++ i) {
		/* ���������� ���������� ������ ������ */
		pthread_join (g_pmsoThreadInfo[i].m_tThreadId, NULL);
		/* ����� ����� �� ���������� */
		g_pmsoThreadInfo[i].m_tThreadId = -1;
		/* ����������� �������, �������� ������ */
		CleanUpThreadInfo (&(g_pmsoThreadInfo[i]));
	}
	/* ����������� ������, ���������� ��� �������� ���������� � ������� */
	if (g_pmsoThreadInfo) {
		delete [] g_pmsoThreadInfo;
		g_pmsoThreadInfo = NULL;
	}
	/* ����������� ������� */
	sem_destroy (&g_tSem);

	LEAVE_ROUT (0);
}

void CleanUpThreadInfo (SThreadInfo *p_psoThreadInfo)
{
	ENTER_ROUT;

	/* ���� � �������� ��������� ������� ������ ��������� */
	if (NULL == p_psoThreadInfo) {
		return;
	}
	/* ����������� ������, ������� �������� ����������� � �� */
	if (p_psoThreadInfo->m_pcoDBConn) {
		p_psoThreadInfo->m_pcoDBConn->logoff ();
		delete p_psoThreadInfo->m_pcoDBConn;
		p_psoThreadInfo->m_pcoDBConn = NULL;
	}
	/* ����������� ������, ������� �������� ����������� � CoASensd */
	if (p_psoThreadInfo->m_pcoIPConn) {
		p_psoThreadInfo->m_pcoIPConn->DisConnect ();
		delete p_psoThreadInfo->m_pcoIPConn;
		p_psoThreadInfo->m_pcoIPConn = NULL;
	}

	LEAVE_ROUT (0);
}

int ThreadManager (const SSubscriberRefresh &p_soRefreshRecord)
{
	int iRetVal = 0;
	int iFnRes;
	unsigned int uiThreadInd;

	ENTER_ROUT;

	do {
		/* ������� ������������ �������� */
		sem_wait (&g_tSem);
		/* ���� ��� ������� ��������� ������� � ������� */
		if (NULL == g_pmsoThreadInfo) {
			iRetVal = -2000;
			break;
		}
		/* ���� ��������� ����� */
		for (uiThreadInd = 0; uiThreadInd < g_uiThreadCount; ++ uiThreadInd) {
			/* ��������� �������� �� ��� ���� ����� */
			if (-1 != g_pmsoThreadInfo[uiThreadInd].m_tThreadId			/* ����� ���������� */
					&& 0 == g_pmsoThreadInfo[uiThreadInd].m_iBusy) {	/* � �� ����� */
				break;
			}
		}
		/* ���� ��������� ����� ����� �� ������� */
		if (uiThreadInd == g_uiThreadCount) {
			iRetVal = -2010;
			g_coLog.WriteLog ("ThreadManager: error: It could not find free thread");
			break;
		}
		/* �������� ������ ����������� ������ */
		g_pmsoThreadInfo[uiThreadInd].m_soSubscriberRefresh = p_soRefreshRecord;
		/* ��������� ��������� ������ */
		g_pmsoThreadInfo[uiThreadInd].m_iBusy = 1;
		/* ��������� ����� � ������ */
		iFnRes = pthread_mutex_unlock (&(g_pmsoThreadInfo[uiThreadInd].m_tMutex));
		if (iFnRes) {
			g_coLog.WriteLog ("ThreadManager: error: pthread_mutex_unlock: code: '%d'", iFnRes);
			iRetVal = -2020;
			break;
		}
	} while (0);

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

void *ThreadWorker (void *p_pvParam)
{
	int iFnRes;

	ENTER_ROUT;

	/* �������� �������� ������ */
	SThreadInfo *psoThreadInfo = reinterpret_cast <SThreadInfo*> (p_pvParam);

	g_coLog.WriteLog ("coam: ThreadWorker: thread started: '%p'", psoThreadInfo->m_tThreadId);

	/* �������������� ��� ���������� ������ */
	psoThreadInfo->m_iRetVal = 0;

	while (0 == psoThreadInfo->m_iExit) {
		/* ���� ������������� ������ */
		iFnRes = pthread_mutex_lock (&(psoThreadInfo->m_tMutex));
		/* ���� ��� ������������� ��������� ������ ������� �� ����� � ��������� ����� */
		if (iFnRes) {
			psoThreadInfo->m_iRetVal = -3000;
			break;
		}
		/* ���� ��������� ���� ��������� ������ ������� �� ����� */
		if (psoThreadInfo->m_iExit) {
			break;
		}
		/* ��������� ��������� ������ */
		iFnRes = OperateSubscriber (psoThreadInfo->m_soSubscriberRefresh, psoThreadInfo->m_pcoIPConn, *(psoThreadInfo->m_pcoDBConn));
		if (0 == iFnRes) {
			/* ���� ������ ������� ���������� ������� �� �� ������� */
			iFnRes = DeleteRefreshRecord (&psoThreadInfo->m_soSubscriberRefresh, *(psoThreadInfo->m_pcoDBConn));
		}
		/* ����������� ����� */
		psoThreadInfo->m_iBusy = 0;
		/* ��������� ������� */
		iFnRes = sem_post (&g_tSem);
		if (iFnRes) { 
			psoThreadInfo->m_iRetVal = -3010;
			break;
		}
	}

	LEAVE_ROUT (psoThreadInfo->m_iRetVal);

	pthread_exit (&(psoThreadInfo->m_iRetVal));
}
