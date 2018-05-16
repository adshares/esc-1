#include "setaccountstatushandler.h"
#include "command/setaccountstatus.h"
#include "../office.hpp"
#include "helper/hash.h"

SetAccountStatusHandler::SetAccountStatusHandler(office& office, boost::asio::ip::tcp::socket& socket)
    : CommandHandler(office, socket) {
}

void SetAccountStatusHandler::onInit(std::unique_ptr<IBlockCommand> command) {
    try {
        m_command = std::unique_ptr<SetAccountStatus>(dynamic_cast<SetAccountStatus*>(command.release()));
    } catch (std::bad_cast& bc) {
        ELOG("SetAccountStatus bad_cast caught: %s\n", bc.what());
        return;
    }
}

void SetAccountStatusHandler::onExecute() {
    assert(m_command);

    ErrorCodes::Code errorCode = ErrorCodes::Code::eNone;
    auto        startedTime     = time(NULL);
    uint32_t    lpath           = startedTime-startedTime%BLOCKSEC;
    int64_t     fee             = m_command->getFee();
    int64_t     deduct          = m_command->getDeduct();

    //commit changes
    m_usera.msid++;
    m_usera.time  = m_command->getTime();
    m_usera.lpath = lpath;

    Helper::create256signhash(m_command->getSignature(), m_command->getSignatureSize(), m_usera.hash, m_usera.hash);

    uint32_t msid;
    uint32_t mpos;

    if(!m_offi.add_msg(*m_command.get(), msid, mpos)) {
        ELOG("ERROR: message submission failed (%08X:%08X)\n",msid, mpos);
        errorCode = ErrorCodes::Code::eMessageSubmitFail;
    }

    if(!errorCode && m_command->getDestBankId() == m_offi.svid) {
        if(!m_offi.set_status(m_command->getDestUserId(), m_command->getStatus())) {
            ELOG("ERROR: status submission failed");
            errorCode = ErrorCodes::Code::eStatusSubmitFail;
        }
    }

    if(!errorCode) {
        m_offi.set_user(m_command->getUserId(), m_usera, deduct+fee);

        log_t tlog;
        tlog.time   = time(NULL);
        tlog.type   = m_command->getType();
        tlog.node   = m_command->getDestBankId();
        tlog.user   = m_command->getDestUserId();
        tlog.umid   = m_command->getUserMessageId();
        tlog.nmid   = msid;
        tlog.mpos   = mpos;

        tInfo info;
        info.weight = m_usera.weight;
        info.deduct = m_command->getDeduct();
        info.fee = m_command->getFee();
        info.stat = m_usera.stat;
        memcpy(info.pkey, m_usera.pkey, sizeof(info.pkey));
        memcpy(tlog.info, &info, sizeof(tInfo));

        tlog.weight = m_command->getStatus();
        m_offi.put_ulog(m_command->getUserId(), tlog);

        if(m_command->getBankId() == m_command->getDestBankId()) {
            tlog.type|=0x8000; //incoming
            tlog.node=m_command->getBankId();
            tlog.user=m_command->getUserId();
            tlog.weight=m_command->getStatus();

            m_offi.put_ulog(m_command->getDestUserId(),tlog);
        }
    }

    try {
        boost::asio::write(m_socket, boost::asio::buffer(&errorCode, ERROR_CODE_LENGTH));
        if(!errorCode) {
            commandresponse response{m_usera, msid, mpos};
            boost::asio::write(m_socket, boost::asio::buffer(&response, sizeof(response)));
        }
    } catch (std::exception& e) {
        DLOG("Responding to client %08X error: %s\n", m_usera.user, e.what());
    }
}

bool SetAccountStatusHandler::onValidate() {

    auto startedTime = time(NULL);
    int32_t diff = m_command->getTime() - startedTime;

    int64_t deduct = m_command->getDeduct();
    int64_t fee = m_command->getFee();

    ErrorCodes::Code errorCode = ErrorCodes::Code::eNone;

    if(diff>1) {
        DLOG("ERROR: time in the future (%d>1s)\n", diff);
        errorCode = ErrorCodes::Code::eTimeInFuture;
    }
    else if(m_command->getBankId()!=m_offi.svid) {
        errorCode = ErrorCodes::Code::eBankNotFound;
    }
    else if(!m_offi.svid) {
        errorCode = ErrorCodes::Code::eBankIncorrect;
    }
    else if(m_offi.readonly) {
        errorCode = ErrorCodes::Code::eReadOnlyMode;
    }
    else if(m_usera.msid != m_command->getUserMessageId()) {
        errorCode = ErrorCodes::Code::eBadMsgId;
    }
    else if(!m_offi.check_user(m_command->getDestBankId(),m_command->getDestUserId())) {
        DLOG("ERROR: bad target user %04X:%08X\n", m_command->getDestBankId(), m_command->getDestUserId());
        errorCode = ErrorCodes::Code::eUserBadTarget;
    }
    else if(m_command->getDestBankId() == m_offi.svid && // check if other admins have write permissions
            m_command->getUserId() &&
            m_command->getUserId()!=m_command->getDestUserId() &&
            (0x0 != (m_command->getStatus()&0xF))) { //normal users can set only higher bits

        DLOG("ERROR: not authorized to change higher bits (%04X) for user %08X \n",
            m_command->getStatus(), m_command->getDestUserId());
        errorCode = ErrorCodes::Code::eAuthorizationError;
    }
    else if(deduct+fee+(m_usera.user ? USER_MIN_MASS:BANK_MIN_UMASS) > m_usera.weight) {
        DLOG("ERROR: too low balance txs:%016lX+fee:%016lX+min:%016lX>now:%016lX\n",
             deduct, fee, (uint64_t)(m_usera.user ? USER_MIN_MASS:BANK_MIN_UMASS), m_usera.weight);
        errorCode = ErrorCodes::Code::eLowBalance;
    }

    if (errorCode) {
        try {
            boost::asio::write(m_socket, boost::asio::buffer(&errorCode, ERROR_CODE_LENGTH));
        } catch (std::exception& e) {
            DLOG("Responding to client %08X error: %s\n", m_usera.user, e.what());
        }
        return false;
    }

    return true;
}