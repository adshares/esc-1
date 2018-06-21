#include "sendonehandler.h"
#include "command/sendone.h"
#include "../office.hpp"
#include "helper/hash.h"


SendOneHandler::SendOneHandler(office& office, boost::asio::ip::tcp::socket& socket)
    : CommandHandler(office, socket) {
}

void SendOneHandler::onInit(std::unique_ptr<IBlockCommand> command) {
    m_command = init<SendOne>(std::move(command));
}

void SendOneHandler::onExecute() {
    assert(m_command);

    auto        startedTime     = time(NULL);
    uint32_t    lpath           = startedTime-startedTime%BLOCKSEC;
    int64_t     fee{0};
    int64_t     deposit{-1}; // if deposit=0 inform target
    int64_t     deduct{0};
    ErrorCodes::Code errorCode = ErrorCodes::Code::eNone;

    deposit = m_command->getDeduct();
    deduct = m_command->getDeduct();
    fee = m_command->getFee();

    //commit changes
    m_usera.msid++;
    m_usera.time=m_command->getTime();
    m_usera.lpath=lpath;

    Helper::create256signhash(m_command->getSignature(), m_command->getSignatureSize(), m_usera.hash, m_usera.hash);

    uint32_t msid;
    uint32_t mpos;

    if(!m_offi.add_msg(m_command->getBlockMessage(), m_command->getBlockMessageSize(), msid, mpos)) {
        DLOG("ERROR: message submission failed (%08X:%08X)\n",msid, mpos);
        errorCode = ErrorCodes::Code::eMessageSubmitFail;
    } else {
        m_offi.set_user(m_command->getUserId(), m_usera, deduct+fee);

        log_t tlog;
        tlog.time   = time(NULL);
        tlog.type   = m_command->getType();
        tlog.node   = m_command->getDestBankId();
        tlog.user   = m_command->getDestUserId();
        tlog.umid   = m_command->getUserMessageId();
        tlog.nmid   = msid;
        tlog.mpos   = mpos;
        tlog.weight = -deduct;
        memcpy(tlog.info, m_command->getInfoMsg(),32);
        m_offi.put_ulog(m_command->getUserId(),  tlog);

        if (m_command->getBankId() == m_command->getDestBankId()) {
            tlog.type|=0x8000; //incoming
            tlog.node=m_command->getBankId();
            tlog.user=m_command->getUserId();
            tlog.weight=deduct;
            m_offi.put_ulog(m_command->getDestUserId(), tlog);
            if(deposit>=0) {
                m_offi.add_deposit(m_command->getDestUserId(), deposit);
            }
        }
    }

    try {
        std::vector<boost::asio::const_buffer> response;
        response.emplace_back(boost::asio::buffer(&errorCode, ERROR_CODE_LENGTH));
        if(!errorCode) {
            commandresponse cresponse{m_usera, msid, mpos};
            response.emplace_back(boost::asio::buffer(&cresponse, sizeof(cresponse)));
        }
        boost::asio::write(m_socket, response);
    } catch (std::exception& e) {
        DLOG("Responding to client %08X error: %s\n", m_usera.user, e.what());
    }
}

ErrorCodes::Code SendOneHandler::onValidate() {
    if(!m_offi.check_user(m_command->getDestBankId(), m_command->getDestUserId())) {
        DLOG("ERROR: bad target: node %04X user %04X\n", m_command->getDestBankId(), m_command->getDestUserId());
        return ErrorCodes::Code::eUserBadTarget;
    }
    return ErrorCodes::Code::eNone;
}
