// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018-2019 The Safe Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "transactiondesc.h"

#include "bitcoinunits.h"
#include "guiutil.h"
#include "paymentserver.h"
#include "transactionrecord.h"

#include "base58.h"
#include "consensus/consensus.h"
#include "validation.h"
#include "script/script.h"
#include "timedata.h"
#include "util.h"
#include "wallet/db.h"
#include "wallet/wallet.h"

#include "instantx.h"
#include "bitcoinunits.h"
#include "main.h"
#include <stdint.h>
#include <string>

extern int g_nStartSPOSHeight;

bool TransactionDesc::needDisplay(int descColumn, int showType, int type)
{
    bool ret = false;
    switch (descColumn)
    {
    case DescColumnStatus:
    {
        if(showType==SHOW_TX||showType==SHOW_LOCKED_TX||showType==SHOW_ASSETS_DISTRIBUTE||showType==SHOW_APPLICATION_REGIST||showType==SHOW_CANDY_TX)
            ret = true;
        break;
    }
    case DescColumnDate:
    {
        ret = true;//all display
        break;
    }
    case DescColumnCredit:
    {
        if(showType==SHOW_TX||showType==SHOW_LOCKED_TX)
            ret = true;
        break;
    }
    case DescColumnDebit:
    {
        if(showType==SHOW_TX||showType==SHOW_LOCKED_TX)
            ret = true;
        break;
    }
    case DescColumnNetAmount:
    {
        if(showType==SHOW_TX||showType==SHOW_LOCKED_TX)
            ret = true;
        break;
    }
    case DescColumnTransactionFee:
    {
        if(showType==SHOW_TX||showType==SHOW_LOCKED_TX)
            ret = true;
        break;
    }
    case DescColumnLockedAmount:
    {
        if(showType==SHOW_LOCKED_TX)
            ret = true;
        break;
    }
    case DescColumnUnlockedHeight:
    {
        if(showType==SHOW_LOCKED_TX)
            ret = true;
        break;
    }
    case DescColumnTransactionID:
    {
        ret = true;//all display
        break;
    }
    case DescColumnTransactionTotalSize:
    {
        if(showType==SHOW_LOCKED_TX)
            ret = true;
        break;
    }
    case DescColumnMessage:
    {
        if(showType==SHOW_TX||showType==SHOW_LOCKED_TX)
            ret = true;
        break;
    }
    case DescColumnMerchant:
    {
        if(showType==SHOW_TX||showType==SHOW_LOCKED_TX)
            ret = true;
        break;
    }
    case DescColumnInput:
    {
        if(showType==SHOW_LOCKED_TX)
            ret = true;
        break;
    }
    case DescColumnComment://all display
    {
        ret = true;
        break;
    }
    case DescAppId:
    case DescAppManagerAddress:
    case DescAppName:
    case DescAppDesc:
    case DescAppDevType:
    case DescAppDevName:
    case DescAppWebUrl:
    case DescAppLogoUrl:
    case DescAppCoverUrl:
    {
        if(showType==SHOW_APPLICATION_REGIST)
            ret = true;
        break;
    }
    case DescAssetsId:
    {
        if(showType==SHOW_ASSETS_DISTRIBUTE)
            ret = true;
        break;
    }
    case DescAssetsName:
    case DescAssetsShortName:
    {
        if((showType==SHOW_ASSETS_DISTRIBUTE&&type==TransactionRecord::FirstDistribute)
                ||(showType==SHOW_CANDY_TX&&type==TransactionRecord::GETCandy))
            ret = true;
        break;
    }
    case DescAssetsDesc:
    case DescAssetsUnit:
    case DescAssetsTotalAmount:
    case DescAssetsFirstIssueAmount:
    case DescAssetsActualAmount:
    case DescAssetsDecimals:
    case DescAssetsDestory:
    case DescAssetsPayCandy:
    case DescAssetsCandyAmount:
    case DescAssetsCandyExpired:
    {
        if(showType==SHOW_ASSETS_DISTRIBUTE&&type==TransactionRecord::FirstDistribute)
            ret = true;
        break;
    }
    case DescAssetsRemarks:
        if(showType==SHOW_ASSETS_DISTRIBUTE)
            ret = true;
        break;
    case DescCommonAssetsId:
    case DescCommonAssetsAmount:
    case DescCommonAssetsRemark:
    {
        if(showType==SHOW_ASSETS_DISTRIBUTE&&type==TransactionRecord::AddDistribute)
            ret = true;
        break;
    }
    case DescGetCandyAmount:
    {
        if((showType==SHOW_CANDY_TX||showType==SHOW_TX)&&type==TransactionRecord::GETCandy)
            ret = true;
        break;
    }
    case DescGetCandyAssetId:
    case DescGetCandyAddress:
    case DescGetCandyRemark:
    {
        if(showType==SHOW_CANDY_TX&&type==TransactionRecord::GETCandy)
            ret = true;
        break;
    }
    default:
        break;
    }
    return ret;
}

QString TransactionDesc::FormatTxStatus(const CWalletTx& wtx)
{
    AssertLockHeld(cs_main);
    if (!CheckFinalTx(wtx))
    {
        if (wtx.nLockTime < LOCKTIME_THRESHOLD)
            return tr("Open for %n more block(s)", "", wtx.nLockTime - chainActive.Height());
        else
            return tr("Open until %1").arg(GUIUtil::dateTimeStr(wtx.nLockTime));
    }
    else
    {
        int nDepth = wtx.GetDepthInMainChain();
        if (nDepth < 0) return tr("conflicted");

        QString strTxStatus;
        bool fOffline = (GetAdjustedTime() - wtx.nTimeReceived > 2 * 60) && (wtx.GetRequestCount() == 0);

        if (fOffline) {
            strTxStatus = tr("%1/offline").arg(nDepth);
        } else if (nDepth == 0) {
            strTxStatus = tr("0/unconfirmed, %1").arg((wtx.InMempool() ? tr("in memory pool") : tr("not in memory pool"))) + (wtx.isAbandoned() ? ", "+tr("abandoned") : "");
        } else if (nDepth < 6) {
            strTxStatus = tr("%1/unconfirmed").arg(nDepth);
        } else {
            strTxStatus = tr("%1 confirmations").arg(nDepth);
        }

        if(!instantsend.HasTxLockRequest(wtx.GetHash())) return strTxStatus; // regular tx

        int nSignatures = instantsend.GetTransactionLockSignatures(wtx.GetHash());
        int nSignaturesMax = CTxLockRequest(wtx).GetMaxSignatures();
        // InstantSend
        strTxStatus += " (";
        if(instantsend.IsLockedInstantSendTransaction(wtx.GetHash())) {
            strTxStatus += tr("verified via InstantSend");
        } else if(!instantsend.IsTxLockCandidateTimedOut(wtx.GetHash())) {
            strTxStatus += tr("InstantSend verification in progress - %1 of %2 signatures").arg(nSignatures).arg(nSignaturesMax);
        } else {
            strTxStatus += tr("InstantSend verification failed");
        }
        strTxStatus += ")";

        return strTxStatus;
    }
}

QString TransactionDesc::toHTML(CWallet *wallet, CWalletTx &wtx, TransactionRecord *rec, int unit, int showType, bool fAssets)
{
    QString strHTML;

    LOCK2(cs_main, wallet->cs_wallet);
    strHTML.reserve(4000);
    strHTML += "<html><font face='verdana, arial, helvetica, sans-serif'>";

    int64_t nTime = wtx.GetTxTime();
    CAmount nCredit = wtx.GetCredit(ISMINE_ALL);
    CAmount nDebit = wtx.GetDebit(ISMINE_ALL);
    CAmount nNet = nCredit - nDebit;

    if(needDisplay(DescColumnStatus,showType))
    {
        strHTML += "<b>" + tr("Status") + ":</b> " + FormatTxStatus(wtx);
        int nRequests = wtx.GetRequestCount();
        if (nRequests != -1)
        {
            if (nRequests == 0)
                strHTML += tr(", has not been successfully broadcast yet");
            else if (nRequests > 0)
                strHTML += tr(", broadcast through %n node(s)", "", nRequests);
        }
        strHTML += "<br>";
    }

    if(needDisplay(DescColumnDate,showType))
        strHTML += "<b>" + tr("Date") + ":</b> " + (nTime ? GUIUtil::dateTimeStr(nTime) : "") + "<br>";

    //
    // From
    //
    if (wtx.IsCoinBase())
    {
        strHTML += "<b>" + tr("Source") + ":</b> " + tr("Generated") + "<br>";
    }
    else if (wtx.mapValue.count("from") && !wtx.mapValue["from"].empty())
    {
        // Online transaction
        strHTML += "<b>" + tr("From") + ":</b> " + GUIUtil::HtmlEscape(wtx.mapValue["from"]) + "<br>";
    }
    else
    {
        // Offline transaction
        if (nNet > 0)
        {
            // Credit
            if (CBitcoinAddress(rec->address).IsValid())
            {
                CTxDestination address = CBitcoinAddress(rec->address).Get();
                if (wallet->mapAddressBook.count(address))
                {
                    strHTML += "<b>" + tr("From") + ":</b> " + tr("unknown") + "<br>";
                    strHTML += "<b>" + tr("To") + ":</b> ";
                    strHTML += GUIUtil::HtmlEscape(rec->address);
                    QString addressOwned = (::IsMine(*wallet, address) == ISMINE_SPENDABLE) ? tr("own address") : tr("watch-only");
                    if (!wallet->mapAddressBook[address].name.empty())
                        strHTML += " (" + addressOwned + ", " + tr("label") + ": " + GUIUtil::HtmlEscape(wallet->mapAddressBook[address].name) + ")";
                    else
                        strHTML += " (" + addressOwned + ")";
                    strHTML += "<br>";
                }
            }
        }
    }

    //
    // To
    //
    if (wtx.mapValue.count("to") && !wtx.mapValue["to"].empty())
    {
        // Online transaction
        std::string strAddress = wtx.mapValue["to"];
        strHTML += "<b>" + tr("To") + ":</b> ";
        CTxDestination dest = CBitcoinAddress(strAddress).Get();
        if (wallet->mapAddressBook.count(dest) && !wallet->mapAddressBook[dest].name.empty())
            strHTML += GUIUtil::HtmlEscape(wallet->mapAddressBook[dest].name) + " ";
        strHTML += GUIUtil::HtmlEscape(strAddress) + "<br>";
    }

    //
    // Amount
    //
    if (wtx.IsCoinBase() && nCredit == 0)
    {
        //
        // Coinbase
        //
        if(!rec->bLocked)
        {
            CAmount nUnmatured = 0;
            BOOST_FOREACH(const CTxOut& txout, wtx.vout)
                nUnmatured += wallet->GetCredit(txout, ISMINE_ALL,txout.IsAsset());
            strHTML += "<b>" + tr("Credit") + ":</b> ";
            if (wtx.IsInMainChain())
                strHTML += BitcoinUnits::formatHtmlWithUnit(unit, nUnmatured)+ " (" + tr("matures in %n more block(s)", "", wtx.GetBlocksToMaturity()) + ")";
            else
                strHTML += "(" + tr("not accepted") + ")";
            strHTML += "<br>";
        }
        else
        {
            if (wtx.IsInMainChain())
                strHTML += tr("matures in %n more block(s)", "", wtx.GetBlocksToMaturity());
            else
                strHTML += tr("not accepted");
            strHTML += "<br>";
        }
    }
    else if (nNet > 0)
    {
        if(!rec->bLocked)
        {
            //
            // Safe Credit
            //
            strHTML += "<b>" + tr("Credit") + ":</b> " + BitcoinUnits::formatHtmlWithUnit(unit, rec->credit + rec->debit) + "<br>";
        }
    }
    else
    {
        //nNet <= 0
        isminetype fAllFromMe = ISMINE_SPENDABLE;
        BOOST_FOREACH(const CTxIn& txin, wtx.vin)
        {
            isminetype mine = wallet->IsMine(txin);
            if(fAllFromMe > mine) fAllFromMe = mine;
        }

        isminetype fAllToMe = ISMINE_SPENDABLE;
        BOOST_FOREACH(const CTxOut& txout, wtx.vout)
        {
            isminetype mine = wallet->IsMine(txout);
            if(fAllToMe > mine) fAllToMe = mine;
        }

        if (fAllFromMe)
        {
            if(fAllFromMe & ISMINE_WATCH_ONLY)
                strHTML += "<b>" + tr("From") + ":</b> " + tr("watch-only") + "<br>";

            //
            // Debit
            //
            BOOST_FOREACH(const CTxOut& txout, wtx.vout)
            {
                // Ignore change
                isminetype toSelf = wallet->IsMine(txout);
                if ((toSelf == ISMINE_SPENDABLE) && (fAllFromMe == ISMINE_SPENDABLE))
                    continue;

                if (!wtx.mapValue.count("to") || wtx.mapValue["to"].empty())
                {
                    // Offline transaction
                    CTxDestination address;
                    if (ExtractDestination(txout.scriptPubKey, address))
                    {
                        if ( showType== SHOW_TX || ( (showType == SHOW_LOCKED_TX) && (rec->type!=TransactionRecord::SendToSelf)
                                                     && IsLockedTxOut(txout.GetHash(), txout)))
                        {
                            strHTML += "<b>" + tr("To") + ":</b> ";
                            if (wallet->mapAddressBook.count(address) && !wallet->mapAddressBook[address].name.empty())
                                strHTML += GUIUtil::HtmlEscape(wallet->mapAddressBook[address].name) + " ";
                            strHTML += GUIUtil::HtmlEscape(CBitcoinAddress(address).ToString());
                            if(toSelf == ISMINE_SPENDABLE)
                                strHTML += " (own address)";
                            else if(toSelf & ISMINE_WATCH_ONLY)
                                strHTML += " (watch-only)";
                            strHTML += "<br>";
                        }
                    }
                }

                if(!rec->bLocked)
                {
                    if(toSelf)
                    {
                        if(txout.IsAsset())
                            strHTML += "<b>" + tr("Credit") + ":</b> " + BitcoinUnits::format(rec->assetsData.nDecimals, txout.nValue, false, BitcoinUnits::separatorAlways,true) + " " + QString::fromStdString(rec->assetsData.strAssetUnit) + "<br>";
                        else
                            strHTML += "<b>" + tr("Credit") + ":</b> " + BitcoinUnits::formatHtmlWithUnit(unit, txout.nValue) + "<br>";
                    }else
                    {
                        if(txout.IsAsset())
                        {
                            if(fAssets)
                                strHTML += "<b>" + tr("Debit") + ":</b> " + BitcoinUnits::format(rec->assetsData.nDecimals, -txout.nValue, false, BitcoinUnits::separatorAlways,true) + " " + QString::fromStdString(rec->assetsData.strAssetUnit) + "<br>";
                        }
                        else
                            strHTML += "<b>" + tr("Debit") + ":</b> " + BitcoinUnits::formatHtmlWithUnit(unit, -txout.nValue) + "<br>";
                    }
                }
            }

            if (fAllToMe)
            {
                if(!rec->bLocked)
                {
                    // Payment to self
                    CAmount nChange = wtx.GetChange(fAssets);
                    CAmount nValue = 0;
                    int tmpUnit = unit;
                    if(fAssets)
                    {
                        tmpUnit = rec->assetsData.nDecimals;
                        CAmount tmpCredit = 0;
                        if(fAllFromMe)
                        {
                            tmpCredit = rec->assetCredit + rec->assetDebit;
                        }
                        else
                        {
                            tmpCredit = wtx.GetCredit(ISMINE_ALL,true,&rec->commonData.assetId);
                        }
                        nValue = tmpCredit;
                    }else
                    {
                        nValue = nCredit - nChange;
                    }
                    if(rec->appHeader.nAppCmd!=ADD_ASSET_CMD)
                        strHTML += "<b>" + tr("Total debit") + ":</b> " + BitcoinUnits::formatHtmlWithUnit(tmpUnit, -nValue,false,BitcoinUnits::separatorAlways,fAssets,QString::fromStdString(rec->assetsData.strAssetUnit)) + "<br>";
                    strHTML += "<b>" + tr("Total credit") + ":</b> " + BitcoinUnits::formatHtmlWithUnit(tmpUnit, nValue,false,BitcoinUnits::separatorAlways,fAssets,QString::fromStdString(rec->assetsData.strAssetUnit)) + "<br>";
                }
            }

            if(!rec->bLocked)
            {
                CAmount nTxFee = nDebit - wtx.GetValueOut();
                if (nTxFee > 0)
                    strHTML += "<b>" + tr("Transaction fee") + ":</b> " + BitcoinUnits::formatHtmlWithUnit(unit, -nTxFee) + "<br>";
            }
        }
        else
        {
            if(rec->bAssets/*||rec->bGetCandy*/)
            {
                CTxDestination address = CBitcoinAddress(rec->address).Get();
                CBitcoinAddress pAddress(address);
                strHTML += "<b>" + tr("Credit") + ":</b> " + BitcoinUnits::formatHtmlWithUnit(rec->assetsData.nDecimals, wtx.GetCredit(ISMINE_ALL,fAssets,&rec->commonData.assetId,&pAddress)
                                                                                              ,false,BitcoinUnits::separatorNever,true,QString::fromStdString(rec->assetsData.strAssetUnit)) + "<br>";
            } else if(!rec->bLocked)
            {
                //
                // Mixed debit transaction
                //
                BOOST_FOREACH(const CTxIn& txin, wtx.vin)
                {
                    if (wallet->IsMine(txin))
                    {
                        if(rec->bSAFETransaction)
                            strHTML += "<b>" + tr("Debit") + ":</b> " + BitcoinUnits::formatHtmlWithUnit(unit, -wallet->GetDebit(txin, ISMINE_ALL)) + "<br>";
                        else if(rec->bAssets)
                            strHTML += "<b>" + tr("Debit") + ":</b> " + BitcoinUnits::formatHtmlWithUnit(rec->assetsData.nDecimals, -wallet->GetDebit(txin, ISMINE_ALL,true,&rec->commonData.assetId)
                                                                                                          ,false,BitcoinUnits::separatorNever,true,QString::fromStdString(rec->assetsData.strAssetUnit)) + "<br>";
                    }
                }
                BOOST_FOREACH(const CTxOut& txout, wtx.vout)
                {
                    if (wallet->IsMine(txout))
                    {
                        if(txout.IsSafeOnly()&&rec->bSAFETransaction)
                            strHTML += "<b>" + tr("Credit") + ":</b> " + BitcoinUnits::formatHtmlWithUnit(unit, wallet->GetCredit(txout, ISMINE_ALL,txout.IsAsset())) + "<br>";
                        else if(rec->bAssets&&txout.IsAsset())
                        {
                            strHTML += "<b>" + tr("Credit") + ":</b> " + BitcoinUnits::formatHtmlWithUnit(rec->assetsData.nDecimals, wallet->GetCredit(txout, ISMINE_ALL,txout.IsAsset())
                                                                                                          ,false,BitcoinUnits::separatorNever,true,QString::fromStdString(rec->assetsData.strAssetUnit)) + "<br>";
                        }
                    }
                }
            }
        }
    }

    if(!rec->bLocked&&needDisplay(DescColumnNetAmount,showType))
        strHTML += "<b>" + tr("Net amount") + ":</b> " + BitcoinUnits::formatHtmlWithUnit(unit, nNet, true) + "<br>";
    else
    {
        if(needDisplay(DescColumnLockedAmount,showType))
        {
            if(rec->bSAFETransaction)
                strHTML += "<b>" + tr("Locked amount") + ":</b> " + BitcoinUnits::formatHtmlWithUnit(unit, rec->nLockedAmount, true) + "<br>";
            else
                strHTML += "<b>" + tr("Locked amount") + ":</b> " + BitcoinUnits::formatHtmlWithUnit(rec->assetsData.nDecimals, rec->nLockedAmount, true,BitcoinUnits::separatorAlways,true,QString::fromStdString(rec->assetsData.strAssetUnit)) + "<br>";
        }
        if(needDisplay(DescColumnUnlockedHeight,showType))
        {
            int64_t unlockedHeight = 0;
            if (wtx.nVersion>= SAFE_TX_VERSION_3)
            {
                unlockedHeight = rec->nUnlockedHeight;
            }
            else
            {
                 if (rec->nTxHeight >=  g_nStartSPOSHeight)
                 {
                    unlockedHeight = rec->nUnlockedHeight * ConvertBlockNum();
                 }
                 else
                 {
                    if (rec->nUnlockedHeight >= g_nStartSPOSHeight)
                    {
                        int nSPOSLaveHeight = (rec->nUnlockedHeight - g_nStartSPOSHeight) * ConvertBlockNum();
                        unlockedHeight = g_nStartSPOSHeight + nSPOSLaveHeight;
                    }
                    else
                        unlockedHeight = rec->nUnlockedHeight;
                 }
            }

            strHTML += "<b>" + tr("Unlocked height") + ":</b> " + QString::number(unlockedHeight) + "<br>";
        }
    }

    //
    // Message
    //
    if (wtx.mapValue.count("message") && !wtx.mapValue["message"].empty()&&needDisplay(DescColumnMessage,showType))
        strHTML += "<b>" + tr("Message") + ":</b>" + GUIUtil::HtmlEscape(wtx.mapValue["message"], true) + "<br>";
    if (wtx.mapValue.count("comment") && !wtx.mapValue["comment"].empty()&&needDisplay(DescColumnComment,showType))//rpc
        strHTML += "<b>" + tr("Comment") + ":</b>" + GUIUtil::HtmlEscape(wtx.mapValue["comment"], true) + "<br>";
    else if(rec->appHeader.nAppCmd == TRANSFER_ASSET_CMD&&needDisplay(DescColumnComment,showType))//ui tranfer asset
        strHTML += "<b>" + tr("Comment") + ":</b>" + GUIUtil::HtmlEscape(rec->commonData.strRemarks, true) + "<br>";
    else if(rec->appHeader.nAppCmd == TRANSFER_SAFE_CMD&&needDisplay(DescColumnComment,showType))//ui tranfer safe
        strHTML += "<b>" + tr("Comment") + ":</b>" + GUIUtil::HtmlEscape(rec->transferSafeData.strRemarks, true) + "<br>";

    //Application desc
    if(needDisplay(DescAppName,showType))
        strHTML += "<b>" + tr("Application Name") + ":</b> " + GUIUtil::HtmlEscape(QString::fromStdString(rec->appData.strAppName)) + "<br>";

    if(needDisplay(DescAppDevType,showType))
    {
        QString devTypeStr  = tr("Company");
        if(rec->appData.nDevType==APP_Dev_Type_Personal){
            devTypeStr = tr("Personal");
        }
        strHTML += "<b>" + tr("Application Dev Type") + ":</b> " + GUIUtil::HtmlEscape(devTypeStr) + "<br>";
    }

    int devType = rec->appData.nDevType;
    if(needDisplay(DescAppDevName,showType))
    {
        QString nameStr  = tr("Company Name");
        if(devType==APP_Dev_Type_Personal){
            nameStr = tr("Personal Name");
        }
        strHTML += "<b>" + nameStr+ ":</b> " + GUIUtil::HtmlEscape(QString::fromStdString(rec->appData.strDevName)) + "<br>";
    }

    if(needDisplay(DescAppWebUrl,showType)&&devType==APP_Dev_Type_Company)
        strHTML += "<b>" + tr("Application Web URL") + ":</b> " + GUIUtil::HtmlEscape(QString::fromStdString(rec->appData.strWebUrl)) + "<br>";

    if(needDisplay(DescAppLogoUrl,showType)&&devType==APP_Dev_Type_Company)
        strHTML += "<b>" + tr("Application Logo URL") + ":</b> " + GUIUtil::HtmlEscape(QString::fromStdString(rec->appData.strLogoUrl)) + "<br>";

    if(needDisplay(DescAppCoverUrl,showType)&&devType==APP_Dev_Type_Company)
        strHTML += "<b>" + tr("Application Cover URL") + ":</b> " + GUIUtil::HtmlEscape(QString::fromStdString(rec->appData.strCoverUrl)) + "<br>";

    if(needDisplay(DescAppDesc,showType))
        strHTML += "<b>" + tr("Application Desc") + ":</b> " + GUIUtil::HtmlEscape(QString::fromStdString(rec->appData.strAppDesc),true) + "<br>";

    if(needDisplay(DescAppId,showType))
        strHTML += "<b>" + tr("Application ID") + ":</b> " + GUIUtil::HtmlEscape(QString::fromStdString(rec->appData.GetHash().GetHex())) + "<br>";

    if(needDisplay(DescAppManagerAddress,showType))
        strHTML += "<b>" + tr("Manager Address") + ":</b> " + GUIUtil::HtmlEscape(QString::fromStdString(rec->address)) + "<br>";


    //Assets
    if(needDisplay(DescAssetsName,showType,rec->type))
        strHTML += "<b>" + tr("Asset Name") + ":</b> " + GUIUtil::HtmlEscape(QString::fromStdString(rec->assetsData.strAssetName)) + "<br>";

    if(needDisplay(DescAssetsShortName,showType,rec->type))
        strHTML += "<b>" + tr("Asset Short Name") + ":</b> " + GUIUtil::HtmlEscape(QString::fromStdString(rec->assetsData.strShortName)) + "<br>";

    if(needDisplay(DescAssetsUnit,showType,rec->type))
        strHTML += "<b>" + tr("Assets Unit") + ":</b> " + GUIUtil::HtmlEscape(QString::fromStdString(rec->assetsData.strAssetUnit)) + "<br>";

    if(needDisplay(DescAssetsTotalAmount,showType,rec->type))
    {
        QString strAmount = BitcoinUnits::formatWithUnit(rec->assetsData.nDecimals,rec->assetsData.nTotalAmount,false,BitcoinUnits::separatorAlways
                                                         ,true,GUIUtil::HtmlEscape(QString::fromStdString(rec->assetsData.strAssetUnit)));
        strHTML += "<b>" + tr("Assets Total Amount") + ":</b> " + strAmount + "<br>";
    }

    if(needDisplay(DescAssetsFirstIssueAmount,showType,rec->type))
    {
        QString strAmount = BitcoinUnits::formatWithUnit(rec->assetsData.nDecimals,rec->assetsData.nFirstIssueAmount,false,BitcoinUnits::separatorAlways
                                                         ,true,GUIUtil::HtmlEscape(QString::fromStdString(rec->assetsData.strAssetUnit)));
        strHTML += "<b>" + tr("Assets First Issue Amount") + ":</b> " + strAmount + "<br>";
    }

    if(needDisplay(DescAssetsActualAmount,showType,rec->type))
    {
        QString strAmount = BitcoinUnits::formatWithUnit(rec->assetsData.nDecimals,rec->assetsData.nFirstActualAmount,false,BitcoinUnits::separatorAlways
                                                         ,true,GUIUtil::HtmlEscape(QString::fromStdString(rec->assetsData.strAssetUnit)));
        strHTML += "<b>" + tr("Assets Actual Amount") + ":</b> " + strAmount + "<br>";
    }

    if(needDisplay(DescAssetsDecimals,showType,rec->type))
        strHTML += "<b>" + tr("Assets Decimals") + ":</b> " + QString::number(rec->assetsData.nDecimals) + "<br>";

    if(needDisplay(DescAssetsDesc,showType,rec->type))
        strHTML += "<b>" + tr("Assets Desc") + ":</b> " + GUIUtil::HtmlEscape(QString::fromStdString(rec->assetsData.strAssetDesc),true) + "<br>";

    if(needDisplay(DescAssetsRemarks,showType,rec->type))
    {
        if(rec->type==TransactionRecord::FirstDistribute)
            strHTML += "<b>" + tr("Assets Remarks") + ":</b> " + GUIUtil::HtmlEscape(QString::fromStdString(rec->assetsData.strRemarks),true) + "<br>";
        else
            strHTML += "<b>" + tr("Add Assets Remarks") + ":</b> " + GUIUtil::HtmlEscape(QString::fromStdString(rec->commonData.strRemarks),true) + "<br>";
    }

    QString strDestory = rec->assetsData.bDestory ? tr("Yes") : tr("No");
    if(needDisplay(DescAssetsDestory,showType,rec->type))
        strHTML += "<b>" + tr("Assets Destory") + ":</b> " + strDestory + "<br>";

    QString strPayCandy = rec->assetsData.bPayCandy ? tr("Yes") : tr("No");
    if(needDisplay(DescAssetsPayCandy,showType,rec->type))
        strHTML += "<b>" + tr("Pay Candy") + ":</b> " + strPayCandy + "<br>";

    if(needDisplay(DescAssetsCandyAmount,showType,rec->type))
    {
        QString strAmount = BitcoinUnits::formatWithUnit(rec->assetsData.nDecimals,rec->assetsData.nCandyAmount,false,BitcoinUnits::separatorAlways
                                                         ,true,GUIUtil::HtmlEscape(QString::fromStdString(rec->assetsData.strAssetUnit)));
        strHTML += "<b>" + tr("Assets Candy Amount") + ":</b> " + strAmount + "<br>";
    }

    if(needDisplay(DescAssetsCandyExpired,showType,rec->type))
        strHTML += "<b>" + tr("Assets Candy Expired") + ":</b> " + GUIUtil::HtmlEscape(QString::number(rec->assetsData.nCandyExpired)) + "<br>";

    if(needDisplay(DescAssetsId,showType,rec->type))
        strHTML += "<b>" + tr("Assets ID") + ":</b> " + QString::fromStdString(rec->assetsData.GetHash().ToString()) + "<br>";

    //Common Assets
    if(needDisplay(DescCommonAssetsId,showType,rec->type))
        strHTML += "<b>" + tr("Add Assets ID") + ":</b> " + QString::fromStdString(rec->commonData.assetId.ToString()) + "<br>";

    if(needDisplay(DescCommonAssetsAmount,showType,rec->type))
    {
        QString strAmount = BitcoinUnits::formatWithUnit(rec->assetsData.nDecimals,rec->commonData.nAmount,false,BitcoinUnits::separatorAlways
                                                         ,true,QString::fromStdString(rec->assetsData.strAssetUnit));
        strHTML += "<b>" + tr("Add Assets Amount") + ":</b> " + strAmount + "<br>";
    }

    if(needDisplay(DescCommonAssetsRemark,showType,rec->type))
        strHTML += "<b>" + tr("Assets Remarks") + ":</b> " + QString::fromStdString(rec->assetsData.strRemarks) + "<br>";

    //Candy
    if(needDisplay(DescGetCandyAmount,showType,rec->type))
    {
        QString strAmount = BitcoinUnits::formatWithUnit(rec->assetsData.nDecimals,rec->getCandyData.nAmount,false,BitcoinUnits::separatorAlways
                                                         ,true,QString::fromStdString(rec->assetsData.strAssetUnit));
        strHTML += "<b>" + tr("Get Candy Amount") + ":</b> " + strAmount + "<br>";
    }

    if(needDisplay(DescGetCandyAddress,showType,rec->type))
        strHTML += "<b>" + tr("Get Candy Address") + ":</b> " + GUIUtil::HtmlEscape(QString::fromStdString(rec->address)) + "<br>";

    if(needDisplay(DescGetCandyAssetId,showType,rec->type)){
        strHTML += "<b>" + tr("Candy Asset ID") + ":</b> " + GUIUtil::HtmlEscape(QString::fromStdString(rec->getCandyData.assetId.ToString())) + "<br>";
    }

    //End
    if(needDisplay(DescColumnTransactionID,showType))
        strHTML += "<b>" + tr("Transaction ID") + ":</b> " + GUIUtil::HtmlEscape(TransactionRecord::formatSubTxId(wtx.GetHash(), rec->idx)) + "<br>";

    if(needDisplay(DescColumnTransactionTotalSize,showType))
        strHTML += "<b>" + tr("Transaction total size") + ":</b> " + QString::number(wtx.GetTotalSize()) + " bytes<br>";

    // Message from normal safe:URI (safe:XyZ...?message=example)
    Q_FOREACH (const PAIRTYPE(std::string, std::string)& r, wtx.vOrderForm)
        if (r.first == "Message" && needDisplay(DescColumnMessage,showType))
            strHTML += "<br><b>" + tr("Message") + ":</b><br>" + GUIUtil::HtmlEscape(r.second, true) + "<br>";

    //
    // PaymentRequest info:
    //
    Q_FOREACH (const PAIRTYPE(std::string, std::string)& r, wtx.vOrderForm)
    {
        if (r.first == "PaymentRequest")
        {
            PaymentRequestPlus req;
            req.parse(QByteArray::fromRawData(r.second.data(), r.second.size()));
            QString merchant;
            if (req.getMerchant(PaymentServer::getCertStore(), merchant)&&needDisplay(DescColumnMerchant,showType))
                strHTML += "<b>" + tr("Merchant") + ":</b> " + GUIUtil::HtmlEscape(merchant) + "<br>";
        }
    }

    if (wtx.IsCoinBase())
    {
        quint32 numBlocksToMaturity = 0;
        if(chainActive.Height()>=g_nStartSPOSHeight)
            numBlocksToMaturity = COINBASE_MATURITY_SPOS +  1;
        else
            numBlocksToMaturity = COINBASE_MATURITY +  1;
        strHTML += "<br>" + tr("Generated coins must mature %1 blocks before they can be spent. When you generated this block, it was broadcast to the network to be added to the block chain. If it fails to get into the chain, its state will change to \"not accepted\" and it won't be spendable. This may occasionally happen if another node generates a block within a few seconds of yours.").arg(QString::number(numBlocksToMaturity)) + "<br>";
    }

    //
    // Debug view
    //
    if (fDebug)
    {
        strHTML += "<hr><br>" + tr("Debug information") + "<br><br>";
        BOOST_FOREACH(const CTxIn& txin, wtx.vin)
            if(wallet->IsMine(txin))
                strHTML += "<b>" + tr("Debit") + ":</b> " + BitcoinUnits::formatHtmlWithUnit(unit, -wallet->GetDebit(txin, ISMINE_ALL)) + "<br>";
        BOOST_FOREACH(const CTxOut& txout, wtx.vout)
        {
            if(wallet->IsMine(txout))
            {
                if(txout.IsAsset())
                {
                    QString strAmount = BitcoinUnits::formatWithUnit(rec->assetsData.nDecimals,wallet->GetCredit(txout, ISMINE_ALL,txout.IsAsset()),false,BitcoinUnits::separatorAlways
                                                                     ,true,GUIUtil::HtmlEscape(QString::fromStdString(rec->assetsData.strAssetUnit)));
                    strHTML += "<b>" + tr("Credit") + ":</b> " + strAmount + "<br>";
                }
                else
                    strHTML += "<b>" + tr("Credit") + ":</b> " + BitcoinUnits::formatHtmlWithUnit(unit, wallet->GetCredit(txout, ISMINE_ALL,txout.IsAsset())) + "<br>";
            }
        }

        strHTML += "<br><b>" + tr("Transaction") + ":</b><br>";
        strHTML += GUIUtil::HtmlEscape(wtx.ToString(), true);

        strHTML += "<br><b>" + tr("Inputs") + ":</b>";
        strHTML += "<ul>";

        BOOST_FOREACH(const CTxIn& txin, wtx.vin)
        {
            COutPoint prevout = txin.prevout;

            CCoins prev;
            if(pcoinsTip->GetCoins(prevout.hash, prev))
            {
                if (prevout.n < prev.vout.size())
                {
                    strHTML += "<li>";
                    const CTxOut &vout = prev.vout[prevout.n];
                    CTxDestination address;
                    if (ExtractDestination(vout.scriptPubKey, address))
                    {
                        if (wallet->mapAddressBook.count(address) && !wallet->mapAddressBook[address].name.empty())
                            strHTML += GUIUtil::HtmlEscape(wallet->mapAddressBook[address].name) + " ";
                        strHTML += QString::fromStdString(CBitcoinAddress(address).ToString());
                    }
                    strHTML = strHTML + " " + tr("Amount") + "=" + BitcoinUnits::formatHtmlWithUnit(unit, vout.nValue);
                    strHTML = strHTML + " IsMine=" + (wallet->IsMine(vout) & ISMINE_SPENDABLE ? tr("true") : tr("false"));
                    strHTML = strHTML + " IsWatchOnly=" + (wallet->IsMine(vout) & ISMINE_WATCH_ONLY ? tr("true") : tr("false")) + "</li>";
                }
            }
        }

        strHTML += "</ul>";
    }

    strHTML += "</font></html>";
    return strHTML;
}
