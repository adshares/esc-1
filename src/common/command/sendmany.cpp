#include "sendmany.h"

#include <set>

#include "ed25519/ed25519.h"
#include "abstraction/interfaces.h"

SendMany::SendMany()
    : m_data{}, m_additionalData(nullptr) {
}

SendMany::SendMany(uint16_t bank, uint32_t user, uint32_t msid, std::vector<SendAmountTxnRecord> &txns_data, uint32_t time)
    : m_data(bank, user, msid, time, txns_data.size()), m_transactions(txns_data), m_additionalData(nullptr) {

    fillAdditionalData();
}

SendMany::~SendMany() {
    delete[] m_additionalData;
}

unsigned char* SendMany::getData() {
    return reinterpret_cast<unsigned char*>(&m_data.info);
}

unsigned char* SendMany::getAdditionalData() {
    // workaround: from server side buffer needs to be alocated before read additional data
    if (!m_additionalData && this->getAdditionalDataSize() > 0) {
        m_additionalData = new unsigned char[this->getAdditionalDataSize()];
    }
    return m_additionalData;
}

int SendMany::getAdditionalDataSize() {
    return (m_data.info.txn_counter * sizeof(SendAmountTxnRecord));
}

unsigned char* SendMany::getResponse() {
    return reinterpret_cast<unsigned char*>(&m_response);
}

void SendMany::setData(char* data) {
    //! NOTE that this function sets only data, without additional data
    m_data = *reinterpret_cast<decltype(m_data)*>(data);
}

void SendMany::setResponse(char* response) {
    m_response = *reinterpret_cast<decltype(m_response)*>(response);
}

int SendMany::getDataSize() {
    return sizeof(m_data.info);
}

int SendMany::getResponseSize() {
    return sizeof(m_response);
}

unsigned char* SendMany::getSignature() {
    return m_data.sign;
}

int SendMany::getSignatureSize() {
    return sizeof(m_data.sign);
}

int SendMany::getType() {
    return TXSTYPE_MPT;
}

void SendMany::sign(const uint8_t* hash, const uint8_t* sk, const uint8_t* pk) {
    int dataSize = this->getDataSize();
    int additionalSize = this->getAdditionalDataSize();
    int size = dataSize + additionalSize;
    unsigned char *fullDataBuffer = new unsigned char[size];

    if (!m_additionalData) {
        fillAdditionalData();
    }

    memcpy(fullDataBuffer, this->getData(), dataSize);
    memcpy(fullDataBuffer + dataSize, this->getAdditionalData(), additionalSize);

    ed25519_sign2(hash,SHA256_DIGEST_LENGTH , fullDataBuffer, size, sk, pk, getSignature());
    delete[] fullDataBuffer;
}

bool SendMany::checkSignature(const uint8_t* hash, const uint8_t* pk) {
    int dataSize = this->getDataSize();
    int additionalSize = this->getAdditionalDataSize();
    int size = dataSize + additionalSize;
    unsigned char *fullDataBuffer = new unsigned char[size];

    if (!m_additionalData) {
        fillAdditionalData();
    }

    memcpy(fullDataBuffer, this->getData(), dataSize);
    memcpy(fullDataBuffer + dataSize, this->getAdditionalData(), additionalSize);

    int result = ed25519_sign_open2(hash,SHA256_DIGEST_LENGTH, fullDataBuffer, size, pk, getSignature());
    delete[] fullDataBuffer;
    return (result == 0);

}

void SendMany::saveResponse(settings& sts) {
    sts.msid = m_response.usera.msid;
    std::copy(m_response.usera.hash, m_response.usera.hash + SHA256_DIGEST_LENGTH, sts.ha.data());
}

uint32_t SendMany::getUserId() {
    return m_data.info.src_user;
}

uint32_t SendMany::getBankId() {
    return m_data.info.src_node;
}

uint32_t SendMany::getTime() {
    return m_data.info.txn_time;
}

int64_t SendMany::getFee() {
    int64_t fee = TXS_MIN_FEE;
    for (auto &it : m_transactions) {
        fee+=TXS_MPT_FEE(it.amount);
        if(m_data.info.src_node!=it.dest_node) {
            fee+=TXS_LNG_FEE(it.amount);
        }
    }
    return fee;
}

int64_t SendMany::getDeduct() {
    int64_t deduct = 0;
    for (auto &it : m_transactions) {
        deduct += it.amount;
    }
    return deduct;
}

user_t& SendMany::getUserInfo() {
    return m_response.usera;
}

bool SendMany::send(INetworkClient& netClient) {
    if(!netClient.sendData(getData(), this->getDataSize())) {
        std::cerr<<"SendMany ERROR sending data";
        return false;
    }

    if(!netClient.sendData(getAdditionalData(), this->getAdditionalDataSize())) {
        std::cerr<<"SendMany ERROR sending additional data";
        return false;
    }

    if(!netClient.sendData(getSignature(), this->getSignatureSize())) {
        std::cerr<<"SendMany ERROR sending signature";
        return false;
    }

    if(!netClient.readData(getResponse(), getResponseSize())) {
        std::cerr<<"SendMany ERROR reading global info\n";
        return false;
    }

    return true;
}

uint32_t SendMany::getUserMessageId() {
    return m_data.info.msg_id;
}

void SendMany::fillAdditionalData() {
    if (!m_additionalData) {
        int size = this->getAdditionalDataSize();
        if (size > 0) {
            m_additionalData = new unsigned char[size];
            unsigned char* dataIt = m_additionalData;
            int itemSize = sizeof(SendAmountTxnRecord);
            for (auto it : m_transactions) {
                memcpy(dataIt, &it, itemSize);
                dataIt += itemSize;
            }
        }
    }
}

void SendMany::initTransactionVector() {
    int size = this->getAdditionalDataSize();
    if (m_additionalData && size > 0) {
        SendAmountTxnRecord txn_record;
        int shift = 0;
        while (shift < size) {
            memcpy(&txn_record, m_additionalData + shift, sizeof(SendAmountTxnRecord));
            m_transactions.push_back(txn_record);
            shift += sizeof(SendAmountTxnRecord);
        }
    }
}

bool SendMany::checkForDuplicates() {
    std::set<std::pair<uint16_t, uint32_t>> checkForDuplicate;
    for (auto &it : m_transactions) {
        uint16_t node = it.dest_node;
        uint32_t user = it.dest_user;
        if (!checkForDuplicate.insert(std::make_pair(node, user)).second) {
            DLOG("ERROR: duplicate target: %04X:%08X\n", node, user);
            return true;
        }
        if (it.amount < 0) {
            DLOG("ERROR: only positive non-zero transactions allowed in MPT\n");
            return true;
        }
    }
    return false;
}

std::vector<SendAmountTxnRecord> SendMany::getTransactionsVector() {
    return m_transactions;
}

std::string SendMany::toString(bool /*pretty*/) {
    return "";
}

boost::property_tree::ptree SendMany::toJson() {
    return boost::property_tree::ptree();
}