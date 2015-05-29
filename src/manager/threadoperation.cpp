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

		/* запрашиваем значение из конфига */
		iFnRes = g_coConf.GetParamValue (pcszConfParamName, strValue);
		if (iFnRes) {
			LOG_F(g_coLog, "config parameter '%s' not found", pcszConfParamName);
			iRetVal = -1000;
			break;
		}
		if (0 == strValue.length ()) {
			LOG_F(g_coLog, "config parameter '%s' not defined", pcszConfParamName);
			iRetVal = -1010;
			break;
		}
		/* преобразуем строку в число */
		g_uiThreadCount = atol(strValue.c_str());
		if (0 == g_uiThreadCount) {
			LOG_F(g_coLog, "invalid '%s' value: '%s'", pcszConfParamName, strValue.c_str ());
			iRetVal = -1020;
			break;
		}
		/* инициализируем семафор */
		iFnRes = sem_init (&g_tThreadSem, 0, g_uiThreadCount);
		if (iFnRes) {
			char mcError[0x400];
			iRetVal = errno;
			strerror_r (iRetVal, mcError, sizeof (mcError));
			LOG_F(g_coLog, "sem_init: error: code '%d'; description: '%s'", iRetVal, mcError);
			iRetVal = -1030;
			break;
		}
		/* выделяем память */
		g_pmsoThreadInfo = new SThreadInfo [g_uiThreadCount];
		if (NULL == g_pmsoThreadInfo) {
			/* память не выделена */
			LOG_F(g_coLog, "can not allocate thread array: out of memory");
			iRetVal = -1040;
			break;
		}
		/* инициализируем массив */
		memset (g_pmsoThreadInfo, 0, sizeof (*g_pmsoThreadInfo) * g_uiThreadCount);
		/* выделяем необходимые ресурсы и создаем потоки */
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
		/* для определенности устанавливаем в '0' код завершения потока */
		p_soThreadInfo.m_iRetVal = 0;
		/* отмечаем, что поток пока не создан */
		p_soThreadInfo.m_tThreadId = -1;
		/* поток рождается свободным */
		p_soThreadInfo.m_iBusy = 0;
		/* и готовым продолжать работу */
		p_soThreadInfo.m_iExit = 0;
		/* инициализируем мьютекс */
		iFnRes = pthread_mutex_init (&(p_soThreadInfo.m_tMutex), NULL);
		if (iFnRes) {
			LOG_F(g_coLog, "pthread_mutex_init error: '%d'", iFnRes);
			iRetVal = -4000;
			break;
		}
		/* инициализируем объкт структуры, содержащей информацию о записях очереди обновления политик */
		memset (&p_soThreadInfo.m_soSubscriberRefresh, 0, sizeof (p_soThreadInfo.m_soSubscriberRefresh));
		/* создаем подключение к CoASensd */
		p_soThreadInfo.m_pcoIPConn = new CIPConnector (10);
		/* после инициализации он нам нужен в закрытом состоянии на всякий случай пытаемся его закрыть */
		pthread_mutex_trylock (&(p_soThreadInfo.m_tMutex));
		/* запуск потока */
		iFnRes = pthread_create (&(p_soThreadInfo.m_tThreadId), NULL, ThreadWorker, &(p_soThreadInfo));
		if (iFnRes) {
			/* отмечаем, что поток не создан */
			p_soThreadInfo.m_tThreadId = -1;
			iRetVal = -4020;
			break;
		}
	} while (0);

	/* если возникла ошибка инициализации потока*/
	if (iRetVal) {
		/* освобождаем занятые ресурсы */
		CleanUpThreadInfo (&p_soThreadInfo);
	}

	return iRetVal;
}

void DeInitThreadPool ()
{
	/* инициируем завершение работы потоков */
	for (unsigned int i = 0; i < g_uiThreadCount; ++ i) {
		/* задаем признак завершения работы потока */
		g_pmsoThreadInfo[i].m_iExit = 1;
		/* разблокируем мьютекс */
		pthread_mutex_unlock (&(g_pmsoThreadInfo[i].m_tMutex));
	}
	/* ожидаем завершения потоков */
	for (unsigned int i = 0; i < g_uiThreadCount; ++ i) {
		/* дожидаемся завершения работы потока */
		pthread_join (g_pmsoThreadInfo[i].m_tThreadId, NULL);
		/* поток более не существует */
		g_pmsoThreadInfo[i].m_tThreadId = -1;
		/* освобождаем ресурсы, выленные потоку */
		CleanUpThreadInfo (&(g_pmsoThreadInfo[i]));
	}
	/* освобождаем память, веделенную для хранения информации о потоках */
	if (g_pmsoThreadInfo) {
		delete [] g_pmsoThreadInfo;
		g_pmsoThreadInfo = NULL;
	}
	/* освобождаем семафор */
	sem_destroy (&g_tThreadSem);
}

void CleanUpThreadInfo (SThreadInfo *p_psoThreadInfo)
{
	/* если в качестве параметра передан пустой указатель */
	if (NULL == p_psoThreadInfo) {
		return;
	}
	/* освобождаем память, занятую объектом подключения к CoASensd */
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
		/* ожидаем освобождения семафора */
		sem_wait (&g_tThreadSem);
		/* если пул потоков уничтожен выходим с ошибкой */
		if (NULL == g_pmsoThreadInfo) {
			iRetVal = -2000;
			break;
		}
		/* ищем свободный поток */
		for (uiThreadInd = 0; uiThreadInd < g_uiThreadCount; ++ uiThreadInd) {
			/* проверяем подходит ли нам этот поток */
			if (-1 != g_pmsoThreadInfo[uiThreadInd].m_tThreadId			/* поток существует */
					&& 0 == g_pmsoThreadInfo[uiThreadInd].m_iBusy) {	/* и не занят */
				break;
			}
		}
		/* если свободный поток найти не удалось */
		if (uiThreadInd == g_uiThreadCount) {
			iRetVal = -2010;
			LOG_E(g_coLog, "it could not find free thread");
			break;
		}
		/* передаем потоку необходимые данные */
		g_pmsoThreadInfo[uiThreadInd].m_soSubscriberRefresh = p_soRefreshRecord;
		/* фиксируем занятость потока */
		g_pmsoThreadInfo[uiThreadInd].m_iBusy = 1;
		/* запускаем поток в работу */
		iFnRes = pthread_mutex_unlock (&(g_pmsoThreadInfo[uiThreadInd].m_tMutex));
		if (iFnRes) {
			LOG_E(g_coLog, "pthread_mutex_unlock: code: '%d'", iFnRes);
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

	/* копируем параметр потока */
	SThreadInfo *psoThreadInfo = reinterpret_cast <SThreadInfo*> (p_pvParam);

	LOG_N(g_coLog, "thread started: '%p'", psoThreadInfo->m_tThreadId);

	/* инициализируем код завершения потока */
	psoThreadInfo->m_iRetVal = 0;

	while (0 == psoThreadInfo->m_iExit) {
		/* ждем разблокировки потока */
		iFnRes = pthread_mutex_lock (&(psoThreadInfo->m_tMutex));
		/* если при разблокировке произошла ошибка выходим из цикла и завершаем поток */
		if (iFnRes) {
			psoThreadInfo->m_iRetVal = -3000;
			break;
		}
		/* если наступила пора завершать работу выходим из цикла */
		if (psoThreadInfo->m_iExit) {
			break;
		}
		/* запрашиваем объект подключения к БД */
		pcoDBConn = db_pool_get();
		/* если объект подключения к БД не получен ничего не делаем, переходим к следущей итерации */
		if (NULL == pcoDBConn)
			continue;
		/* запускаем обработку записи */
		iFnRes = OperateSubscriber (psoThreadInfo->m_soSubscriberRefresh, psoThreadInfo->m_pcoIPConn, *pcoDBConn);
		if (0 == iFnRes) {
			/* если запись успешно обработана удаляем ее из очереди */
			iFnRes = DeleteRefreshRecord (&psoThreadInfo->m_soSubscriberRefresh, *pcoDBConn);
		}
		/* освобождаем подклчение к БД */
		if (pcoDBConn) {
			db_pool_release(pcoDBConn);
			pcoDBConn = NULL;
		}
		/* освобождаем поток */
		psoThreadInfo->m_iBusy = 0;
		/* отпускаем семафор */
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
