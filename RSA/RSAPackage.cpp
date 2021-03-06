#include "RSAPackage.h"

RSAKey RSAPackage::getPublicKey()
{
    return publicKey;
}

FDR RSAPackage::getFDR()
{
    return fdr;
}

int RSAPackage::getAnswerFDR()
{
    return answerFDR;
}

char *RSAPackage::getNonceA()
{
    return nonceA;
}

char *RSAPackage::getNonceB()
{
    return nonceB;
}

char RSAPackage::getACK()
{
    return ack;
}

void RSAPackage::setPublicKey(RSAKey key)
{
    publicKey = key;
}

void RSAPackage::setFDR(FDR fdr)
{
    this->fdr = fdr;
}

void RSAPackage::setAnswerFDR(int answerFDR)
{
    this->answerFDR = answerFDR;
}

void RSAPackage::setNonceA(char *nonce)
{
    strncpy(nonceA, nonce, sizeof(nonceA));
}

void RSAPackage::setNonceB(char *nonce)
{
    strncpy(nonceB, nonce, sizeof(nonceB));
}

void RSAPackage::setACK()
{
    ack = ACK;
}

string RSAPackage::toString()
{
    std::string result =    std::to_string(publicKey.d)    + " | " +
                        std::to_string(publicKey.n)    + " | " +
                        std::to_string(answerFDR)      + " | " +
                        fdr.toString()                 + " | " + 
                        nonceA                         + " | " +
                        nonceB;
    return result;
}