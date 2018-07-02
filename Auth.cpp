#include "Auth.h"

Auth::Auth()
{
    memset(buffer, 0, sizeof(buffer));
}

/*  Armazena o valor do nonce B em uma variável global. */
void Auth::storeNonceA(char *nonce)
{
    strncpy(nonceA, nonce, sizeof(nonceA));
}

/*  Gera um valor para o nonce B.   */
void Auth::generateNonce(char *nonce)
{
    string message = stringTime() + *serverIP + *clientIP + to_string(sequence++);
    string hash = iotAuth.hash(&message);

    memset(nonce, '\0', 129);
    strncpy(nonce, hash.c_str(), 128);
}

/*  Decifra o hash utilizando a chave pública do Cliente. */
string Auth::decryptHash(int *encryptedHash)
{
    byte *decryptedHash = iotAuth.decryptRSA(encryptedHash, rsaStorage->getPartnerPublicKey(), 128);

    char aux;
    string decryptedHashString = "";
    for (int i = 0; i < 128; i++)
    {
        aux = decryptedHash[i];
        decryptedHashString += aux;
    }

    delete[] decryptedHash;

    return decryptedHashString;
}

/*  Inicializa os valores pertinentes a troca de chaves Diffie-Hellman:
    expoente, base, módulo, resultado e a chave de sessão.
*/
void Auth::generateDiffieHellman()
{
    diffieHellmanStorage = new DHStorage();
    diffieHellmanStorage->setBase(iotAuth.randomNumber(100) + 2);
    diffieHellmanStorage->setExponent(iotAuth.randomNumber(3) + 2);
    diffieHellmanStorage->setModulus(iotAuth.randomNumber(100) + 2);
}

/*  Step 1
    Recebe um pedido de início de conexão por parte do Cliente.
*/
void Auth::recv_syn(Socket *soc)
{
    structSyn received;
    recvfrom(soc->socket, &received, sizeof(syn), 0, soc->client, &soc->size);

    start = currentTime();

    /* Verifica se a mensagem recebida é um HELLO. */
    if (received.message == SYN)
    {

        /******************** Store Nonce A ********************/
        storeNonceA(received.nonce);

        /******************** Verbose ********************/
        if (VERBOSE)
            recv_syn_verbose(nonceA);

        send_ack(soc);
    }
    else
    {
        throw DENIED;
    }
}

/*  Step 2
    Envia confirmação ao Cliente referente ao pedido de início de conexão.
*/
void Auth::send_ack(Socket *soc)
{
    /******************** Init Sequence ********************/
    sequence = iotAuth.randomNumber(9999);

    /******************** Generate Nounce B ********************/
    generateNonce(nonceB);

    /******************** Mount Package ********************/
    structAck toSend;
    strncpy(toSend.nonceA, nonceA, sizeof(toSend.nonceA));
    strncpy(toSend.nonceB, nonceB, sizeof(toSend.nonceB));

    /******************** Start Network Time ********************/
    t1 = currentTime();

    /******************** Send Package ********************/
    sendto(soc->socket, &toSend, sizeof(ack), 0, soc->client, soc->size);

    /******************** Verbose ********************/
    if (VERBOSE)
        send_ack_verbose(nonceB, sequence, serverIP, clientIP);

    recv_rsa(soc);
}

/*  Step 3
    Recebe os dados RSA vindos do Cliente.
*/
void Auth::recv_rsa(Socket *soc)
{
    /******************** Receive Exchange ********************/
    RSAKeyExchange rsaReceived;
    int recv = recvfrom(soc->socket, &rsaReceived, sizeof(RSAKeyExchange), 0, soc->client, &soc->size);

    if (recv > 0)
    {

        if (checkRFT(rsaReceived))
        {
            rft(soc);
        }
        else
        {
            /******************** Stop Network Time ********************/
            t2 = currentTime();
            networkTime = elapsedTime(t1, t2);

            /******************** Start Processing Time ********************/
            t1 = currentTime();

            /******************** Store RSA Data ********************/
            RSAPackage rsaPackage = *rsaReceived.getRSAPackage();

            rsaStorage = new RSAStorage();
            rsaStorage->setPartnerPublicKey(rsaPackage.getPublicKey());
            rsaStorage->setPartnerFDR(rsaPackage.getFDR());

            /******************** Decrypt Hash ********************/
            string rsaString = rsaPackage.toString();
            string decryptedHash = decryptHash(rsaReceived.getEncryptedHash());

            /******************** Store TP ********************/
            tp = rsaReceived.getProcessingTime();

            /******************** Store Nonce A ********************/
            storeNonceA(rsaPackage.getNonceA());

            /******************** Validity Hash ********************/
            bool isHashValid = iotAuth.isHashValid(&rsaString, &decryptedHash);
            bool isNonceTrue = strcmp(rsaPackage.getNonceB(), nonceB) == 0;

            /******************** Verbose ********************/
            if (VERBOSE)
                recv_rsa_verbose(rsaStorage, nonceA, isHashValid, isNonceTrue);

            if (isHashValid && isNonceTrue)
            {
                send_rsa(soc);
            }
            else if (!isHashValid)
            {
                throw HASH_INVALID;
            }
            else
            {
                throw NONCE_INVALID;
            }
        }
    }
    else
    {
        if (VERBOSE)
            response_timeout_verbose();
        throw NO_REPLY;
    }
}

/*  Step 4
    Realiza o envio dos dados RSA para o Cliente.
*/
void Auth::send_rsa(Socket *soc)
{
    /******************** Start Auxiliar Time ********************/
    t_aux1 = currentTime();

    /******************** Get Answer FDR ********************/
    int answerFdr = rsaStorage->getPartnerFDR()->getValue(rsaStorage->getPartnerPublicKey()->d);

    /******************** Generate RSA Keys and FDR ********************/
    rsaStorage->setKeyPair(iotAuth.generateRSAKeyPair());
    rsaStorage->setMyFDR(iotAuth.generateFDR());

    /******************** Generate Nonce ********************/
    generateNonce(nonceB);

    /******************** Mount Package ********************/
    RSAPackage rsaSent;
    rsaSent.setPublicKey(*rsaStorage->getMyPublicKey());
    rsaSent.setAnswerFDR(answerFdr);
    rsaSent.setFDR(*rsaStorage->getMyFDR());
    rsaSent.setNonceA(nonceA);
    rsaSent.setNonceB(nonceB);

    /******************** Get Hash ********************/
    string packageString = rsaSent.toString();
    string hash = iotAuth.hash(&packageString);

    /******************** Encrypt Hash ********************/
    int *const encryptedHash = iotAuth.encryptRSA(&hash, rsaStorage->getMyPrivateKey(), 128);

    /******************** Stop Processing Time ********************/
    t2 = currentTime();
    processingTime1 = elapsedTime(t1, t2);

    /******************** Stop Auxiliar Time ********************/
    t_aux2 = currentTime();
    auxiliarTime = elapsedTime(t_aux1, t_aux2);

    /******************** Rectify Network Time ********************/
    networkTime = networkTime - auxiliarTime;

    /******************** Mount Exchange ********************/
    RSAKeyExchange rsaExchange;
    rsaExchange.setRSAPackage(&rsaSent);
    rsaExchange.setEncryptedHash(encryptedHash);
    rsaExchange.setProcessingTime(processingTime1);

    /******************** Start Total Time ********************/
    t1 = currentTime();

    /******************** Send Exchange ********************/
    sendto(soc->socket, (RSAKeyExchange *)&rsaExchange, sizeof(RSAKeyExchange), 0, soc->client, soc->size);

    /******************** Memory Release ********************/
    delete[] encryptedHash;

    /******************** Verbose ********************/
    if (VERBOSE)
        send_rsa_verbose(rsaStorage, sequence, nonceB);

    recv_rsa_ack(soc);
}

/*  Step 5
    Recebe confirmação do Cliente referente ao recebimento dos dados RSA.
*/
void Auth::recv_rsa_ack(Socket *soc)
{
    RSAKeyExchange rsaReceived;
    int recv = recvfrom(soc->socket, &rsaReceived, sizeof(RSAKeyExchange), 0, soc->client, &soc->size);

    if (recv > 0)
    {
        if (checkRFT(rsaReceived))
        {
            rft(soc);
        }
        else
        {
            /******************** Stop Total Time ********************/
            t2 = currentTime();
            totalTime = elapsedTime(t1, t2);

            /******************** Proof of Time ********************/
            // double limit = processingTime1 + networkTime + (processingTime1 + networkTime)*0.1;
            double limit = 1000;

            if (totalTime <= limit)
            {
                /******************** Get Package ********************/
                RSAPackage rsaPackage = *rsaReceived.getRSAPackage();

                /******************** Decrypt Hash ********************/
                string rsaString = rsaPackage.toString();
                string decryptedHash = decryptHash(rsaReceived.getEncryptedHash());

                /******************** Store Nonce A ********************/
                storeNonceA(rsaPackage.getNonceA());

                bool isHashValid = iotAuth.isHashValid(&rsaString, &decryptedHash);
                bool isNonceTrue = strcmp(rsaPackage.getNonceB(), nonceB) == 0;
                bool isAnswerCorrect = iotAuth.isAnswerCorrect(rsaStorage->getMyFDR(), rsaStorage->getMyPublicKey()->d, rsaPackage.getAnswerFDR());

                if (VERBOSE)
                    recv_rsa_ack_verbose(nonceA, isHashValid, isAnswerCorrect, isNonceTrue);

                /******************** Validity ********************/
                if (isHashValid && isNonceTrue && isAnswerCorrect)
                {
                    send_dh(soc);
                }
                else if (!isHashValid)
                {
                    throw HASH_INVALID;
                }
                else if (!isNonceTrue)
                {
                    throw NONCE_INVALID;
                }
                else
                {
                    throw FDR_INVALID;
                }
            }
            else
            {
                if (VERBOSE)
                    time_limit_burst_verbose();
                throw TIMEOUT;
            }
        }
    }
    else
    {
        if (VERBOSE)
            response_timeout_verbose();

        throw NO_REPLY;
    }
}

/*  Step 6
    Realiza o envio dos dados Diffie-Hellman para o Cliente.
*/
void Auth::send_dh(Socket *soc)
{
    /******************** Start Processing Time 2 ********************/
    t_aux1 = currentTime();

    /******************** Generate Diffie-Hellman ********************/
    generateDiffieHellman();

    /******************** Generate Nonce B ********************/
    generateNonce(nonceB);

    /******************** Generate IV ********************/
    int iv = iotAuth.randomNumber(90);
    diffieHellmanStorage->setIV(iv);

    /***************** Mount Package ******************/
    DiffieHellmanPackage dhPackage;
    dhPackage.setResult(diffieHellmanStorage->calculateResult());
    dhPackage.setBase(diffieHellmanStorage->getBase());
    dhPackage.setModulus(diffieHellmanStorage->getModulus());
    dhPackage.setNonceA(nonceA);
    dhPackage.setNonceB(nonceB);
    dhPackage.setIV(iv);

    /******************** Get Hash ********************/
    string packageString = dhPackage.toString();
    string hash = iotAuth.hash(&packageString);

    /******************** Encrypt Hash ********************/
    int *const encryptedHash = iotAuth.encryptRSA(&hash, rsaStorage->getMyPrivateKey(), hash.length());

    /******************** Mount Exchange ********************/
    DHKeyExchange dhSent;
    dhSent.setEncryptedHash(encryptedHash);
    dhSent.setDiffieHellmanPackage(dhPackage);

    /********************** Serialization Exchange **********************/
    byte *const dhExchangeBytes = new byte[sizeof(DHKeyExchange)];
    ObjectToBytes(dhSent, dhExchangeBytes, sizeof(DHKeyExchange));

    /******************** Encryption Exchange ********************/
    int *const encryptedExchange = iotAuth.encryptRSA(dhExchangeBytes, rsaStorage->getPartnerPublicKey(), sizeof(DHKeyExchange));
    delete[] dhExchangeBytes;

    /******************** Stop Processing Time 2 ********************/
    t_aux2 = currentTime();
    processingTime2 = elapsedTime(t1, t2);

    /******************** Mount Enc Packet ********************/
    DHEncPacket encPacket;
    encPacket.setEncryptedExchange(encryptedExchange);

    encPacket.setTP(processingTime2);

    /******************** Start Total Time ********************/
    t1 = currentTime();

    /******************** Send Exchange ********************/
    sendto(soc->socket, (DHEncPacket *)&encPacket, sizeof(DHEncPacket), 0, soc->client, soc->size);

    /******************** Verbose ********************/
    if (VERBOSE)
        send_dh_verbose(&dhPackage, sequence, encPacket.getTP());

    /******************** Memory Release ********************/
    delete[] encryptedHash;
    delete[] encryptedExchange;

    recv_dh(soc);
}

/*  Step 7
    Recebe os dados Diffie-Hellman vindos do Cliente.   */
int Auth::recv_dh(Socket *soc)
{
    /******************** Recv Enc Packet ********************/
    DHEncPacket encPacket;
    int recv = recvfrom(soc->socket, &encPacket, sizeof(DHEncPacket), 0, soc->client, &soc->size);

    if (recv > 0)
    {

        if (checkRFT(encPacket))
        {
            rft(soc);
        }
        else
        {
            /******************** Stop Total Time ********************/
            t2 = currentTime();
            totalTime = elapsedTime(t1, t2);

            /******************** Time of Proof ********************/
            // double limit = networkTime + processingTime2*2;
            double limit = 4000;

            if (totalTime <= limit)
            {

                /******************** Decrypt Exchange ********************/
                DHKeyExchange dhKeyExchange;
                int *const encryptedExchange = encPacket.getEncryptedExchange();
                byte *const dhExchangeBytes = iotAuth.decryptRSA(encryptedExchange, rsaStorage->getMyPrivateKey(), sizeof(DHKeyExchange));

                BytesToObject(dhExchangeBytes, dhKeyExchange, sizeof(DHKeyExchange));
                delete[] dhExchangeBytes;

                /******************** Get DH Package ********************/
                DiffieHellmanPackage dhPackage = dhKeyExchange.getDiffieHellmanPackage();

                /******************** Decrypt Hash ********************/
                string decryptedHash = decryptHash(dhKeyExchange.getEncryptedHash());

                /******************** Validity ********************/
                string dhString = dhPackage.toString();
                const bool isHashValid = iotAuth.isHashValid(&dhString, &decryptedHash);
                const bool isNonceTrue = strcmp(dhPackage.getNonceB(), nonceB) == 0;

                if (isHashValid && isNonceTrue)
                {
                    /******************** Store Nounce A ********************/
                    storeNonceA(dhPackage.getNonceA());
                    /******************** Calculate Session Key ********************/
                    diffieHellmanStorage->setSessionKey(diffieHellmanStorage->calculateSessionKey(dhPackage.getResult()));

                    if (VERBOSE)
                        recv_dh_verbose(&dhPackage, diffieHellmanStorage->getSessionKey(), isHashValid, isNonceTrue);

                    send_dh_ack(soc);
                }
                else if (!isHashValid)
                {
                    throw HASH_INVALID;
                }
                else
                {
                    throw NONCE_INVALID;
                }
            }
            else
            {
                if (VERBOSE)
                    time_limit_burst_verbose();
                throw TIMEOUT;
            }
        }
    }
    else
    {
        if (VERBOSE)
            response_timeout_verbose();
        throw NO_REPLY;
    }
}

/*  Step 8
    Envia confirmação para o Cliente referente ao recebimento dos dados Diffie-Hellman.
*/
void Auth::send_dh_ack(Socket *soc)
{
    /******************** Mount ACK ********************/
    DH_ACK ack;
    ack.message = ACK;
    strncpy(ack.nonce, nonceA, sizeof(ack.nonce));

    // /******************** Serialize ACK ********************/
    byte *const ackBytes = new byte[sizeof(DH_ACK)];
    ObjectToBytes(ack, ackBytes, sizeof(DH_ACK));

    /******************** Encrypt ACK ********************/
    int *const encryptedAck = iotAuth.encryptRSA(ackBytes, rsaStorage->getMyPrivateKey(), sizeof(DH_ACK));
    delete[] ackBytes;

    /******************** Send ACK ********************/
    sendto(soc->socket, (int *)encryptedAck, sizeof(DH_ACK) * sizeof(int), 0, soc->client, soc->size);

    delete[] encryptedAck;

    /******************** Verbose ********************/
    if (VERBOSE)
        send_dh_ack_verbose(&ack);

    connected = true;

    data_transfer(soc);
}

/*  Data Transfer
    Realiza a transferência de dados cifrados para o Cliente.
*/
void Auth::data_transfer(Socket *soc)
{
    delete rsaStorage;

    while (1)
    {
        /********************* Recebimento dos Dados Cifrados *********************/
        char message[1333];
        memset(message, '\0', sizeof(message));
        int recv = recvfrom(soc->socket, message, sizeof(message) - 1, 0, soc->client, &soc->size);

        if (recv > 0)
        {
            /******************* Verifica Pedido de Fim de Conexão ********************/
            cout << "RECEIVED: " << message << endl;

            if (checkRFT(message))
            {
                rft(soc);
            }
            else
            {
                /* Converte o array de chars (buffer) em uma string. */
                string encryptedMessage(message);

                /* Inicialização dos vetores ciphertext. */
                char ciphertextChar[encryptedMessage.length()];
                uint8_t ciphertext[encryptedMessage.length()];
                memset(ciphertext, '\0', encryptedMessage.length());

                /* Inicialização do vetor plaintext. */
                uint8_t plaintext[encryptedMessage.length()];
                memset(plaintext, '\0', encryptedMessage.length());

                /* Inicialização da chave e iv. */
                uint8_t key[32];
                for (int i = 0; i < 32; i++)
                {
                    key[i] = diffieHellmanStorage->getSessionKey();
                }

                uint8_t iv[16];
                for (int i = 0; i < 16; i++)
                {
                    iv[i] = diffieHellmanStorage->getIV();
                }

                /* Converte a mensagem recebida (HEXA) para o array de char ciphertextChar. */
                HexStringToCharArray(&encryptedMessage, encryptedMessage.length(), ciphertextChar);

                /* Converte ciphertextChar em um array de uint8_t (ciphertext). */
                CharToUint8_t(ciphertextChar, ciphertext, encryptedMessage.length());

                /* Decifra a mensagem em um vetor de uint8_t. */
                uint8_t *decrypted = iotAuth.decryptAES(ciphertext, key, iv, encryptedMessage.length());
                cout << "Decrypted: " << decrypted << endl
                     << endl;
            }
        }
    }
}

/*  Done
    Envia um pedido de término de conexão ao Cliente, e seta o estado atual
    para WDC (Waiting Done Confirmation).
*/
void Auth::done(Socket *soc)
{
    sendto(soc->socket, DONE_MESSAGE, sizeof(DONE_MESSAGE), 0, soc->client, soc->size);

    if (VERBOSE)
        done_verbose();

    wdc(soc);
}

/*  Request for Termination
    Envia uma confirmação (DONE_ACK) para o pedido de término de conexão
    vindo do Cliente, e seta o estado para HELLO.
*/
void Auth::rft(Socket *soc)
{
    sendto(soc->socket, DONE_ACK, strlen(DONE_ACK), 0, soc->client, soc->size);
    connected = false;

    if (VERBOSE)
        rft_verbose();
}

/*  Waiting Done Confirmation
    Verifica se a mensagem vinda do Cliente é uma confirmação do pedido de
    fim de conexão enviado pelo Servidor (DONE_ACK).
    Em caso positivo, altera o estado para HELLO, senão, mantém em WDC. 7
*/
void Auth::wdc(Socket *soc)
{
    char message[2];
    int recv = recvfrom(soc->socket, message, sizeof(message), 0, soc->client, &soc->size);

    if (recv > 0)
    {
        if (message[0] == DONE_ACK_CHAR)
        {
            if (VERBOSE)
                wdc_verbose();
            connected = false;
        }
        else
        {
            throw DENIED;
        }
    }
    else
    {
        throw NO_REPLY;
    }
}

int Auth::wait()
{
    meuSocket = socket(PF_INET, SOCK_DGRAM, 0);
    servidor.sin_family = AF_INET;
    servidor.sin_port = htons(DEFAULT_PORT);
    servidor.sin_addr.s_addr = INADDR_ANY;

    bind(meuSocket, (struct sockaddr *)&servidor, sizeof(struct sockaddr_in));

    tam_cliente = sizeof(struct sockaddr_in);

    /* Set maximum wait time for response */
    struct timeval tv;
    tv.tv_sec = TIMEOUT_SEC;
    tv.tv_usec = TIMEOUT_MIC;
    setsockopt(meuSocket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);

    /* Get IP Address Server */
    struct hostent *server;
    char host_name[256];
    gethostname(host_name, sizeof(host_name));
    server = gethostbyname(host_name);
    serverIP = inet_ntoa(*(struct in_addr *)*server->h_addr_list);

    /* Get IP Address Client */
    struct hostent *client;
    char client_name[256];
    gethostname(client_name, sizeof(client_name));
    client = gethostbyname(client_name);
    clientIP = inet_ntoa(*(struct in_addr *)*client->h_addr_list);

    Socket soc = {meuSocket, (struct sockaddr *)&cliente, tam_cliente};

    try
    {
        recv_syn(&soc);
    }
    catch (Reply e)
    {
        reply_verbose(e);
        return e;
    }

    return OK;
}