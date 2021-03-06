#include "stdafx.h"
#include "Level2UserApi.h"
#include "../include/QueueEnum.h"
#include "../include/QueueHeader.h"

#include "../include/ApiHeader.h"
#include "../include/ApiStruct.h"

#include "../include/toolkit.h"

#include "../QuantBox_Queue/MsgQueue.h"

#include <mutex>
#include <vector>
#include <cstring>

using namespace std;

void* __stdcall Query(char type, void* pApi1, void* pApi2, double double1, double double2, void* ptr1, int size1, void* ptr2, int size2, void* ptr3, int size3)
{
	// 由内部调用，不用检查是否为空
	CLevel2UserApi* pApi = (CLevel2UserApi*)pApi2;
	pApi->QueryInThread(type, pApi1, pApi2, double1, double2, ptr1, size1, ptr2, size2, ptr3, size3);
	return nullptr;
}

void CLevel2UserApi::QueryInThread(char type, void* pApi1, void* pApi2, double double1, double double2, void* ptr1, int size1, void* ptr2, int size2, void* ptr3, int size3)
{
	int iRet = 0;
	switch (type)
	{
	case E_Init:
		iRet = _Init();
		break;
	case E_ReqUserLoginField:
		iRet = _ReqUserLogin(type, pApi1, pApi2, double1, double2, ptr1, size1, ptr2, size2, ptr3, size3);
	default:
		break;
	}

	if (0 == iRet)
	{
		//返回成功，填加到已发送池
		m_nSleep = 1;
	}
	else
	{
		m_msgQueue_Query->Input_Copy(type, pApi1, pApi2, double1, double2, ptr1, size1, ptr2, size2, ptr3, size3);
		//失败，按4的幂进行延时，但不超过1s
		m_nSleep *= 4;
		m_nSleep %= 1023;
	}
	this_thread::sleep_for(chrono::milliseconds(m_nSleep));
}

CLevel2UserApi::CLevel2UserApi(void)
{
	m_pApi = nullptr;
	m_lRequestID = 0;
	m_nSleep = 1;

	// 自己维护两个消息队列
	m_msgQueue = new CMsgQueue();
	m_msgQueue_Query = new CMsgQueue();

	m_msgQueue_Query->Register((void*)Query,this);
	m_msgQueue_Query->StartThread();
}

CLevel2UserApi::~CLevel2UserApi(void)
{
	Disconnect();
}

void CLevel2UserApi::Register(void* pCallback, void* pClass)
{
	m_pClass = pClass;
	if (m_msgQueue == nullptr)
		return;

	m_msgQueue_Query->Register((void*)Query, this);
	m_msgQueue->Register(pCallback, this);
	if (pCallback)
	{
		m_msgQueue_Query->StartThread();
		m_msgQueue->StartThread();
	}
	else
	{
		m_msgQueue_Query->StopThread();
		m_msgQueue->StopThread();
	}
}

bool CLevel2UserApi::IsErrorRspInfo(CSecurityFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	bool bRet = ((pRspInfo) && (pRspInfo->ErrorID != 0));
	if (bRet)
	{
		ErrorField* pField = (ErrorField*)m_msgQueue->new_block(sizeof(ErrorField));

		pField->ErrorID = pRspInfo->ErrorID;
		strcpy(pField->ErrorMsg, pRspInfo->ErrorMsg);

		m_msgQueue->Input_NoCopy(ResponeType::OnRtnError, m_msgQueue, m_pClass, bIsLast, 0, pField, sizeof(ErrorField), nullptr, 0, nullptr, 0);
	}
	return bRet;
}

bool CLevel2UserApi::IsErrorRspInfo(CSecurityFtdcRspInfoField *pRspInfo)
{
	bool bRet = ((pRspInfo) && (pRspInfo->ErrorID != 0));

	return bRet;
}

void CLevel2UserApi::Connect(const string& szPath,
	ServerInfoField* pServerInfo,
	UserInfoField* pUserInfo)
{
	m_szPath = szPath;
	memcpy(&m_ServerInfo, pServerInfo, sizeof(ServerInfoField));
	memcpy(&m_UserInfo, pUserInfo, sizeof(UserInfoField));

	m_msgQueue_Query->Input_NoCopy(RequestType::E_Init, m_msgQueue_Query, this, 0, 0,
		nullptr, 0, nullptr, 0, nullptr, 0);
}

int CLevel2UserApi::_Init()
{
	m_pApi = CSecurityFtdcL2MDUserApi::CreateFtdcL2MDUserApi(m_ServerInfo.IsMulticast);

	m_msgQueue->Input_NoCopy(ResponeType::OnConnectionStatus, m_msgQueue, m_pClass, ConnectionStatus::Initialized, 0, nullptr, 0, nullptr, 0, nullptr, 0);

	if (m_pApi)
	{
		m_pApi->RegisterSpi(this);

		//添加地址
		size_t len = strlen(m_ServerInfo.Address) + 1;
		char* buf = new char[len];
		strncpy(buf, m_ServerInfo.Address, len);

		char* token = strtok(buf, _QUANTBOX_SEPS_);
		while (token)
		{
			if (strlen(token)>0)
			{
				m_pApi->RegisterFront(token);
			}
			token = strtok(NULL, _QUANTBOX_SEPS_);
		}
		delete[] buf;

		//初始化连接
		m_pApi->Init();
		m_msgQueue->Input_NoCopy(ResponeType::OnConnectionStatus, m_msgQueue, m_pClass, ConnectionStatus::Connecting, 0, nullptr, 0, nullptr, 0, nullptr, 0);
	}

	return 0;
}

void CLevel2UserApi::ReqUserLogin()
{
	CSecurityFtdcUserLoginField* pBody = (CSecurityFtdcUserLoginField*)m_msgQueue_Query->new_block(sizeof(CSecurityFtdcUserLoginField));

	strncpy(pBody->BrokerID, m_ServerInfo.BrokerID, sizeof(TSecurityFtdcBrokerIDType));
	strncpy(pBody->UserID, m_UserInfo.UserID, sizeof(TSecurityFtdcUserIDType));
	strncpy(pBody->Password, m_UserInfo.Password, sizeof(TSecurityFtdcPasswordType));

	m_msgQueue_Query->Input_NoCopy(RequestType::E_ReqUserLoginField, m_msgQueue_Query, this, 0, 0,
		pBody, sizeof(CSecurityFtdcUserLoginField), nullptr, 0, nullptr, 0);
}

int CLevel2UserApi::_ReqUserLogin(char type, void* pApi1, void* pApi2, double double1, double double2, void* ptr1, int size1, void* ptr2, int size2, void* ptr3, int size3)
{
	m_msgQueue->Input_NoCopy(ResponeType::OnConnectionStatus, m_msgQueue, m_pClass, ConnectionStatus::Logining, 0, nullptr, 0, nullptr, 0, nullptr, 0);
	return m_pApi->ReqUserLogin((CSecurityFtdcUserLoginField*)ptr1, ++m_lRequestID);
}

void CLevel2UserApi::Disconnect()
{
	// 清理查询队列
	if (m_msgQueue_Query)
	{
		m_msgQueue_Query->StopThread();
		m_msgQueue_Query->Register(nullptr,nullptr);
		m_msgQueue_Query->Clear();
		delete m_msgQueue_Query;
		m_msgQueue_Query = nullptr;
	}

	if (m_pApi)
	{
		m_pApi->RegisterSpi(NULL);
		m_pApi->Release();
		m_pApi = NULL;

		// 全清理，只留最后一个
		m_msgQueue->Clear();
		m_msgQueue->Input_NoCopy(ResponeType::OnConnectionStatus, m_msgQueue, m_pClass, ConnectionStatus::Disconnected, 0, nullptr, 0, nullptr, 0, nullptr, 0);
		// 主动触发
		m_msgQueue->Process();
	}

	// 清理响应队列
	if (m_msgQueue)
	{
		m_msgQueue->StopThread();
		m_msgQueue->Register(nullptr,nullptr);
		m_msgQueue->Clear();
		delete m_msgQueue;
		m_msgQueue = nullptr;
	}
}

void CLevel2UserApi::OnFrontConnected()
{
	m_msgQueue->Input_NoCopy(ResponeType::OnConnectionStatus, m_msgQueue, m_pClass, ConnectionStatus::Connected, 0, nullptr, 0, nullptr, 0, nullptr, 0);

	//连接成功后自动请求登录
	ReqUserLogin();
}

void CLevel2UserApi::OnFrontDisconnected(int nReason)
{
	RspUserLoginField* pField = (RspUserLoginField*)m_msgQueue->new_block(sizeof(RspUserLoginField));
	//连接失败返回的信息是拼接而成，主要是为了统一输出
	pField->ErrorID = nReason;
	GetOnFrontDisconnectedMsg(nReason, pField->ErrorMsg);

	m_msgQueue->Input_NoCopy(ResponeType::OnConnectionStatus, m_msgQueue, m_pClass, ConnectionStatus::Disconnected, 0, pField, sizeof(RspUserLoginField), nullptr, 0, nullptr, 0);
}

void CLevel2UserApi::OnRspUserLogin(CSecurityFtdcUserLoginField *pUserLogin, CSecurityFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	RspUserLoginField* pField = (RspUserLoginField*)m_msgQueue->new_block(sizeof(RspUserLoginField));

	if (!IsErrorRspInfo(pRspInfo)
		&& pUserLogin)
	{
		//GetExchangeTime(pUserLogin->TradingDay, nullptr, nullptr,
		//	&pField->TradingDay, nullptr, &pField->LoginTime, nullptr);
		pField->TradingDay = GetDate(pUserLogin->TradingDay);
		//pField->LoginTime = GetTime(pUserLogin->LoginTime);

		//strncpy(pField->LoginTime, pUserLogin->, sizeof(TimeType));
		//sprintf(pField->SessionID, "%d:%d", pUserLogin->FrontID, pRspUserLogin->SessionID);


		//有可能断线了，本处是断线重连后重新订阅
		map<string,set<string> > mapOld = m_mapSecurityIDs;//记下上次订阅的合约

		for(map<string,set<string> >::iterator i=mapOld.begin();i!=mapOld.end();++i)
		{
			string strkey = i->first;
			set<string> setValue = i->second;

			SubscribeL2MarketData(setValue, strkey);//订阅
		}

		mapOld = m_mapIndexIDs;//记下上次订阅的合约

		for(map<string,set<string> >::iterator i=mapOld.begin();i!=mapOld.end();++i)
		{
			string strkey = i->first;
			set<string> setValue = i->second;

			SubscribeL2Index(setValue, strkey);//订阅
		}
	}
	else
	{
		pField->ErrorID = pRspInfo->ErrorID;
		strncpy(pField->ErrorMsg, pRspInfo->ErrorMsg, sizeof(pRspInfo->ErrorMsg));

		m_msgQueue->Input_NoCopy(ResponeType::OnConnectionStatus, m_msgQueue, m_pClass, ConnectionStatus::Disconnected, 0, pField, sizeof(RspUserLoginField), nullptr, 0, nullptr, 0);
	}
}

void CLevel2UserApi::OnRspError(CSecurityFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	IsErrorRspInfo(pRspInfo, nRequestID, bIsLast);
}

void CLevel2UserApi::SubscribeL2MarketData(const string& szInstrumentIDs, const string& szExchageID)
{
	if(nullptr == m_pApi)
		return;

	vector<char*> vct;
	set<char*> st;

	lock_guard<mutex> cl(m_csMapSecurityIDs);

	set<string> _setInstrumentIDs;
	map<string, set<string> >::iterator it = m_mapSecurityIDs.find(szExchageID);
	if (it != m_mapSecurityIDs.end())
	{
		_setInstrumentIDs = it->second;
	}

	char* pBuf = GetSetFromString(szInstrumentIDs.c_str(), _QUANTBOX_SEPS_, vct, st, 1, _setInstrumentIDs);
	m_mapSecurityIDs[szExchageID] = _setInstrumentIDs;

	if (vct.size()>0)
	{
		//转成字符串数组
		char** pArray = new char*[vct.size()];
		for (size_t j = 0; j<vct.size(); ++j)
		{
			pArray[j] = vct[j];
		}

		//订阅
		m_pApi->SubscribeL2MarketData(pArray, (int)vct.size(), (char*)(szExchageID.c_str()));

		delete[] pArray;
	}
	delete[] pBuf;
}

void CLevel2UserApi::UnSubscribeL2MarketData(const string& szInstrumentIDs, const string& szExchageID)
{
	if (nullptr == m_pApi)
		return;

	vector<char*> vct;
	set<char*> st;

	lock_guard<mutex> cl(m_csMapSecurityIDs);

	set<string> _setInstrumentIDs;
	map<string, set<string> >::iterator it = m_mapSecurityIDs.find(szExchageID);
	if (it != m_mapSecurityIDs.end())
	{
		_setInstrumentIDs = it->second;
	}

	char* pBuf = GetSetFromString(szInstrumentIDs.c_str(), _QUANTBOX_SEPS_, vct, st, -1, _setInstrumentIDs);
	m_mapSecurityIDs[szExchageID] = _setInstrumentIDs;

	if (vct.size()>0)
	{
		//转成字符串数组
		char** pArray = new char*[vct.size()];
		for (size_t j = 0; j<vct.size(); ++j)
		{
			pArray[j] = vct[j];
		}

		//订阅
		m_pApi->UnSubscribeL2MarketData(pArray, (int)vct.size(), (char*)(szExchageID.c_str()));

		delete[] pArray;
	}
	delete[] pBuf;
}

void CLevel2UserApi::SubscribeL2MarketData(const set<string>& instrumentIDs, const string& szExchageID)
{
	if(nullptr == m_pApi)
		return;

	string szInstrumentIDs;
	for(set<string>::iterator i=instrumentIDs.begin();i!=instrumentIDs.end();++i)
	{
		szInstrumentIDs.append(*i);
		szInstrumentIDs.append(";");
	}

	if (szInstrumentIDs.length()>1)
	{
		SubscribeL2MarketData(szInstrumentIDs, szExchageID);
	}
}

void CLevel2UserApi::OnRspSubL2MarketData(CSecurityFtdcSpecificInstrumentField *pSpecificInstrument, CSecurityFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	//在模拟平台可能这个函数不会触发，所以要自己维护一张已经订阅的合约列表
	if(!IsErrorRspInfo(pRspInfo,nRequestID,bIsLast)
		&& pSpecificInstrument)
	{
		lock_guard<mutex> cl(m_csMapSecurityIDs);

		set<string> _setInstrumentIDs;
		map<string, set<string> >::iterator it = m_mapSecurityIDs.find(pSpecificInstrument->ExchangeID);
		if (it!=m_mapSecurityIDs.end())
		{
			_setInstrumentIDs = it->second;
		}

		_setInstrumentIDs.insert(pSpecificInstrument->InstrumentID);
		m_mapSecurityIDs[pSpecificInstrument->ExchangeID] = _setInstrumentIDs;
	}
}

void CLevel2UserApi::OnRspUnSubL2MarketData(CSecurityFtdcSpecificInstrumentField *pSpecificInstrument, CSecurityFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	//模拟平台可能这个函数不会触发
	if(!IsErrorRspInfo(pRspInfo,nRequestID,bIsLast)
		&& pSpecificInstrument)
	{
		lock_guard<mutex> cl(m_csMapSecurityIDs);

		set<string> _setInstrumentIDs;
		map<string, set<string> >::iterator it = m_mapSecurityIDs.find(pSpecificInstrument->ExchangeID);
		if (it!=m_mapSecurityIDs.end())
		{
			_setInstrumentIDs = it->second;
		}
		_setInstrumentIDs.erase(pSpecificInstrument->InstrumentID);
		m_mapSecurityIDs[pSpecificInstrument->ExchangeID] = _setInstrumentIDs;
	}
}

void CLevel2UserApi::OnRtnL2MarketData(CSecurityFtdcL2MarketDataField *pL2MarketData)
{
	DepthMarketDataField* pField = (DepthMarketDataField*)m_msgQueue->new_block(sizeof(DepthMarketDataField));
	strncpy(pField->InstrumentID, pL2MarketData->InstrumentID, sizeof(InstrumentIDType));
	strncpy(pField->ExchangeID, pL2MarketData->ExchangeID, sizeof(ExchangeIDType));

	sprintf(pField->Symbol, "%s.%s", pField->InstrumentID, pField->ExchangeID);

	GetExchangeTime(pL2MarketData->TradingDay, pL2MarketData->TradingDay, pL2MarketData->TimeStamp
		, &pField->TradingDay, &pField->ActionDay, &pField->UpdateTime, &pField->UpdateMillisec);

	pField->LastPrice = pL2MarketData->LastPrice;
	pField->Volume = pL2MarketData->TotalTradeVolume;
	pField->Turnover = pL2MarketData->TotalTradeValue;
	//marketData.OpenInterest = pL2MarketData->OpenInterest;
	//marketData.AveragePrice = pL2MarketData->AveragePrice;

	pField->OpenPrice = pL2MarketData->OpenPrice;
	pField->HighestPrice = pL2MarketData->HighPrice;
	pField->LowestPrice = pL2MarketData->LowPrice;
	pField->ClosePrice = pL2MarketData->ClosePrice;
	//marketData.SettlementPrice = pL2MarketData->SettlementPrice;

	//marketData.UpperLimitPrice = pL2MarketData->UpperLimitPrice;
	//marketData.LowerLimitPrice = pL2MarketData->LowerLimitPrice;
	//marketData.PreClosePrice = pL2MarketData->PreClosePrice;
	//marketData.PreSettlementPrice = pL2MarketData->PreSettlementPrice;
	//marketData.PreOpenInterest = pL2MarketData->PreOpenInterest;

	do
	{
		if (pL2MarketData->BidVolume1 == 0)
			break;
		pField->BidPrice1 = pL2MarketData->BidPrice1;
		pField->BidVolume1 = pL2MarketData->BidVolume1;

		if (pL2MarketData->BidVolume2 == 0)
			break;
		pField->BidPrice2 = pL2MarketData->BidPrice2;
		pField->BidVolume2 = pL2MarketData->BidVolume2;

		if (pL2MarketData->BidVolume3 == 0)
			break;
		pField->BidPrice3 = pL2MarketData->BidPrice3;
		pField->BidVolume3 = pL2MarketData->BidVolume3;

		if (pL2MarketData->BidVolume4 == 0)
			break;
		pField->BidPrice4 = pL2MarketData->BidPrice4;
		pField->BidVolume4 = pL2MarketData->BidVolume4;

		if (pL2MarketData->BidVolume5 == 0)
			break;
		pField->BidPrice5 = pL2MarketData->BidPrice5;
		pField->BidVolume5 = pL2MarketData->BidVolume5;
	} while (false);

	do
	{
		if (pL2MarketData->OfferVolume1 == 0)
			break;
		pField->AskPrice1 = pL2MarketData->OfferPrice1;
		pField->AskVolume1 = pL2MarketData->OfferVolume1;

		if (pL2MarketData->OfferVolume2 == 0)
			break;
		pField->AskPrice2 = pL2MarketData->OfferPrice2;
		pField->AskVolume2 = pL2MarketData->OfferVolume2;

		if (pL2MarketData->OfferVolume3 == 0)
			break;
		pField->AskPrice3 = pL2MarketData->OfferPrice3;
		pField->AskVolume3 = pL2MarketData->OfferVolume3;

		if (pL2MarketData->OfferVolume4 == 0)
			break;
		pField->AskPrice4 = pL2MarketData->OfferPrice4;
		pField->AskVolume4 = pL2MarketData->OfferVolume4;

		if (pL2MarketData->OfferVolume5 == 0)
			break;
		pField->AskPrice5 = pL2MarketData->OfferPrice5;
		pField->AskVolume5 = pL2MarketData->OfferVolume5;
	} while (false);

	m_msgQueue->Input_NoCopy(ResponeType::OnRtnDepthMarketData, m_msgQueue, m_pClass, 0, 0, pField, sizeof(DepthMarketDataField), nullptr, 0, nullptr, 0);
}

void CLevel2UserApi::SubscribeL2Index(const string& szInstrumentIDs, const string& szExchageID)
{
	if (nullptr == m_pApi)
		return;

	vector<char*> vct;
	set<char*> st;

	lock_guard<mutex> cl(m_csMapIndexIDs);

	set<string> _setInstrumentIDs;
	map<string, set<string> >::iterator it = m_mapIndexIDs.find(szExchageID);
	if (it != m_mapIndexIDs.end())
	{
		_setInstrumentIDs = it->second;
	}

	char* pBuf = GetSetFromString(szInstrumentIDs.c_str(), _QUANTBOX_SEPS_, vct, st, 1, _setInstrumentIDs);
	m_mapIndexIDs[szExchageID] = _setInstrumentIDs;

	if (vct.size()>0)
	{
		//转成字符串数组
		char** pArray = new char*[vct.size()];
		for (size_t j = 0; j<vct.size(); ++j)
		{
			pArray[j] = vct[j];
		}

		//订阅
		m_pApi->SubscribeL2Index(pArray, (int)vct.size(), (char*)(szExchageID.c_str()));

		delete[] pArray;
	}
	delete[] pBuf;
}

void CLevel2UserApi::UnSubscribeL2Index(const string& szInstrumentIDs, const string& szExchageID)
{
	if (nullptr == m_pApi)
		return;

	vector<char*> vct;
	set<char*> st;

	lock_guard<mutex> cl(m_csMapIndexIDs);

	set<string> _setInstrumentIDs;
	map<string, set<string> >::iterator it = m_mapIndexIDs.find(szExchageID);
	if (it != m_mapIndexIDs.end())
	{
		_setInstrumentIDs = it->second;
	}

	char* pBuf = GetSetFromString(szInstrumentIDs.c_str(), _QUANTBOX_SEPS_, vct, st, -1, _setInstrumentIDs);
	m_mapIndexIDs[szExchageID] = _setInstrumentIDs;

	if (vct.size()>0)
	{
		//转成字符串数组
		char** pArray = new char*[vct.size()];
		for (size_t j = 0; j<vct.size(); ++j)
		{
			pArray[j] = vct[j];
		}

		//订阅
		m_pApi->UnSubscribeL2Index(pArray, (int)vct.size(), (char*)(szExchageID.c_str()));

		delete[] pArray;
	}
	delete[] pBuf;
}

void CLevel2UserApi::SubscribeL2Index(const set<string>& instrumentIDs, const string& szExchageID)
{
	if (nullptr == m_pApi)
		return;

	string szInstrumentIDs;
	for (set<string>::iterator i = instrumentIDs.begin(); i != instrumentIDs.end(); ++i)
	{
		szInstrumentIDs.append(*i);
		szInstrumentIDs.append(";");
	}

	if (szInstrumentIDs.length()>1)
	{
		UnSubscribeL2Index(szInstrumentIDs, szExchageID);
	}
}

void CLevel2UserApi::OnRspSubL2Index(CSecurityFtdcSpecificInstrumentField *pSpecificInstrument, CSecurityFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	//在模拟平台可能这个函数不会触发，所以要自己维护一张已经订阅的合约列表
	if (!IsErrorRspInfo(pRspInfo, nRequestID, bIsLast)
		&& pSpecificInstrument)
	{
		lock_guard<mutex> cl(m_csMapIndexIDs);

		set<string> _setInstrumentIDs;
		map<string, set<string> >::iterator it = m_mapIndexIDs.find(pSpecificInstrument->ExchangeID);
		if (it != m_mapIndexIDs.end())
		{
			_setInstrumentIDs = it->second;
		}

		_setInstrumentIDs.insert(pSpecificInstrument->InstrumentID);
		m_mapIndexIDs[pSpecificInstrument->ExchangeID] = _setInstrumentIDs;
	}
}

void CLevel2UserApi::OnRspUnSubL2Index(CSecurityFtdcSpecificInstrumentField *pSpecificInstrument, CSecurityFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	//模拟平台可能这个函数不会触发
	if (!IsErrorRspInfo(pRspInfo, nRequestID, bIsLast)
		&& pSpecificInstrument)
	{
		lock_guard<mutex> cl(m_csMapIndexIDs);

		set<string> _setInstrumentIDs;
		map<string, set<string> >::iterator it = m_mapIndexIDs.find(pSpecificInstrument->ExchangeID);
		if (it != m_mapIndexIDs.end())
		{
			_setInstrumentIDs = it->second;
		}
		_setInstrumentIDs.erase(pSpecificInstrument->InstrumentID);
		m_mapIndexIDs[pSpecificInstrument->ExchangeID] = _setInstrumentIDs;
	}
}

void CLevel2UserApi::OnRtnL2Index(CSecurityFtdcL2IndexField *pL2Index)
{
	DepthMarketDataField* pField = (DepthMarketDataField*)m_msgQueue->new_block(sizeof(DepthMarketDataField));
	strncpy(pField->InstrumentID, pL2Index->InstrumentID, sizeof(InstrumentIDType));
	strncpy(pField->ExchangeID, pL2Index->ExchangeID, sizeof(ExchangeIDType));

	sprintf(pField->Symbol, "%s.%s", pField->InstrumentID, pField->ExchangeID);
	GetExchangeTime(pL2Index->TradingDay, pL2Index->TradingDay, pL2Index->TimeStamp
		, &pField->TradingDay, &pField->ActionDay, &pField->UpdateTime, &pField->UpdateMillisec);

	pField->LastPrice = pL2Index->LastIndex;
	pField->Volume = pL2Index->TotalVolume;
	pField->Turnover = pL2Index->TurnOver;
	//marketData.OpenInterest = pL2Index->OpenInterest;
	//marketData.AveragePrice = pL2Index->AveragePrice;

	pField->OpenPrice = pL2Index->OpenIndex;
	pField->HighestPrice = pL2Index->HighIndex;
	pField->LowestPrice = pL2Index->LowIndex;
	pField->ClosePrice = pL2Index->CloseIndex;
	//marketData.SettlementPrice = pL2Index->SettlementPrice;

	//marketData.UpperLimitPrice = pL2Index->UpperLimitPrice;
	//marketData.LowerLimitPrice = pL2Index->LowerLimitPrice;
	pField->PreClosePrice = pL2Index->PreCloseIndex;
	//marketData.PreSettlementPrice = pL2Index->PreSettlementPrice;
	//marketData.PreOpenInterest = pL2Index->PreOpenInterest;

	//marketData.BidPrice1 = pL2MarketData->BidPrice1;
	//marketData.BidVolume1 = pL2MarketData->BidVolume1;
	//marketData.AskPrice1 = pL2MarketData->OfferPrice1;
	//marketData.AskVolume1 = pL2MarketData->OfferVolume1;

	////if (pDepthMarketData->BidPrice2 != DBL_MAX || pDepthMarketData->AskPrice2 != DBL_MAX)
	//{
	//	marketData.BidPrice2 = pL2MarketData->BidPrice2;
	//	marketData.BidVolume2 = pL2MarketData->BidVolume2;
	//	marketData.AskPrice2 = pL2MarketData->OfferPrice2;
	//	marketData.AskVolume2 = pL2MarketData->OfferVolume2;

	//	marketData.BidPrice3 = pL2MarketData->BidPrice3;
	//	marketData.BidVolume3 = pL2MarketData->BidVolume3;
	//	marketData.AskPrice3 = pL2MarketData->OfferPrice3;
	//	marketData.AskVolume3 = pL2MarketData->OfferVolume3;

	//	marketData.BidPrice4 = pL2MarketData->BidPrice4;
	//	marketData.BidVolume4 = pL2MarketData->BidVolume4;
	//	marketData.AskPrice4 = pL2MarketData->OfferPrice2;
	//	marketData.AskVolume4 = pL2MarketData->OfferVolume4;

	//	marketData.BidPrice5 = pL2MarketData->BidPrice5;
	//	marketData.BidVolume5 = pL2MarketData->BidVolume5;
	//	marketData.AskPrice5 = pL2MarketData->OfferPrice5;
	//	marketData.AskVolume5 = pL2MarketData->OfferVolume5;
	//}

	m_msgQueue->Input_NoCopy(ResponeType::OnRtnDepthMarketData, m_msgQueue, m_pClass, DepthLevelType::L0, 0, pField, sizeof(DepthMarketDataField), nullptr, 0, nullptr, 0);
}
