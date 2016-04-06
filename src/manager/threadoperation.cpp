#include "utils/config/config.h"
#include "utils/log/log.h"

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
sem_t g_tThreadSem;

int InitThreadPool ()
{
	int iRetVal = 0;
	int iFnRes;

	do {
		std::string strValue;
		const char *pcszConfParamName = "thread_count";

		/* ����������� �������� �� ������� */
		iFnRes = g_coConf.GetParamValue (pcszConfParamName, strValue);
		if (iFnRes) {
			UTL_LOG_F(g_coLog, "config parameter '%s' not found", pcszConfParamName);
			iRetVal = -1000;
			break;
		}
		if (0 == strValue.length ()) {
			UTL_LOG_F(g_coLog, "config parameter '%s' not defined", pcszConfParamName);
			iRetVal = -1010;
			break;
		}
		/* ����������� ������ � ����� */
		g_uiThreadCount = atol(strValue.c_str());
		if (0 == g_uiThreadCount) {
			UTL_LOG_F(g_coLog, "invalid '%s' value: '%s'", pcszConfParamName, strValue.c_str ());
			iRetVal = -1020;
			break;
		}
		/* �������������� ������� */
		iFnRes = sem_init (&g_tThreadSem, 0, g_uiThreadCount);
		if (iFnRes) {
			char mcError[0x400];
			iRetVal = errno;
			strerror_r (iRetVal, mcError, sizeof (mcError));
			UTL_LOG_F(g_coLog, "sem_init: error: code '%d'; description: '%s'", iRetVal, mcError);
			iRetVal = -1030;
			break;
		}
		/* �������� ������ */
		g_pmsoThreadInfo = new SThreadInfo [g_uiThreadCount];
		if (NULL == g_pmsoThreadInfo) {
			/* ������ �� �������� */
			UTL_LOG_F(g_coLog, "can not allocate thread array: out of memory");
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

	return iRetVal;
}

int InitThread (SThreadInfo &p_soThreadInfo)
{
	int iRetVal = 0;
	int iFnRes;

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
			UTL_LOG_F(g_coLog, "pthread_mutex_init error: '%d'", iFnRes);
			iRetVal = -4000;
			break;
		}
		/* �������������� ����� ���������, ���������� ���������� � ������� ������� ���������� ������� */
		memset (&p_soThreadInfo.m_soSubscriberRefresh, 0, sizeof (p_soThreadInfo.m_soSubscriberRefresh));
		/* ������� ����������� � CoASensd */
		p_soThreadInfo.m_pcoIPConn = new CIPConnector (10);
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

	return iRetVal;
}

void DeInitThreadPool ()
{
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
	sem_destroy (&g_tThreadSem);
}

void CleanUpThreadInfo (SThreadInfo *p_psoThreadInfo)
{
	/* ���� � �������� ��������� ������� ������ ��������� */
	if (NULL == p_psoThreadInfo) {
		return;
	}
	/* ����������� ������, ������� �������� ����������� � CoASensd */
	if (p_psoThreadInfo->m_pcoIPConn) {
		p_psoThreadInfo->m_pcoIPConn->DisConnect ();
		delete p_psoThreadInfo->m_pcoIPConn;
		p_psoThreadInfo->m_pcoIPConn = NULL;
	}
}

int ThreadManager (const SSubscriberRefresh &p_soRefreshRecord)
{
	int iRetVal = 0;
	int iFnRes;
	unsigned int uiThreadInd;

	do {
		/* ������� ������������ �������� */
		sem_wait (&g_tThreadSem);
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
			UTL_LOG_E(g_coLog, "it could not find free thread");
			break;
		}
		/* �������� ������ ����������� ������ */
		g_pmsoThreadInfo[uiThreadInd].m_soSubscriberRefresh = p_soRefreshRecord;
		/* ��������� ��������� ������ */
		g_pmsoThreadInfo[uiThreadInd].m_iBusy = 1;
		/* ��������� ����� � ������ */
		iFnRes = pthread_mutex_unlock (&(g_pmsoThreadInfo[uiThreadInd].m_tMutex));
		if (iFnRes) {
			UTL_LOG_E(g_coLog, "pthread_mutex_unlock: code: '%d'", iFnRes);
			iRetVal = -2020;
			break;
		}
	} while (0);

	return iRetVal;
}

void *ThreadWorker (void *p_pvParam)
{
	int iFnRes;
	otl_connect *pcoDBConn = NULL;

	/* �������� �������� ������ */
	SThreadInfo *psoThreadInfo = reinterpret_cast <SThreadInfo*> (p_pvParam);

	UTL_LOG_N(g_coLog, "thread started: '%p'", psoThreadInfo->m_tThreadId);

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
		/* ����������� ������ ����������� � �� */
		pcoDBConn = db_pool_get();
		/* ���� ������ ����������� � �� �� ������� ������ �� ������, ��������� � �������� �������� */
		if (NULL == pcoDBConn)
			continue;
		/* ��������� ��������� ������ */
		iFnRes = OperateSubscriber (psoThreadInfo->m_soSubscriberRefresh, psoThreadInfo->m_pcoIPConn, *pcoDBConn);
		if (0 == iFnRes) {
			/* ���� ������ ������� ���������� ������� �� �� ������� */
			iFnRes = DeleteRefreshRecord (&psoThreadInfo->m_soSubscriberRefresh, *pcoDBConn);
		}
		/* ����������� ���������� � �� */
		if (pcoDBConn) {
			db_pool_release(pcoDBConn);
			pcoDBConn = NULL;
		}
		/* ����������� ����� */
		psoThreadInfo->m_iBusy = 0;
		/* ��������� ������� */
		iFnRes = sem_post (&g_tThreadSem);
		if (iFnRes) { 
			psoThreadInfo->m_iRetVal = -3010;
			break;
		}
	}

	if (pcoDBConn)
		db_pool_release(pcoDBConn);

	pthread_exit (&(psoThreadInfo->m_iRetVal));
}
