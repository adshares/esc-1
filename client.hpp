#ifndef CLIENT_HPP
#define CLIENT_HPP

//this could all go to the office class and we could use just the start() function

class client : public boost::enable_shared_from_this<client>
{
public:
  client(boost::asio::io_service& io_service,office& offi)
    : socket_(io_service),
      offi_(offi),
      addr(""),
      port(""),
      buf(NULL),
      len(0),
      more(0)
  {  //read_msg_ = boost::make_shared<message>();
    std::cerr<<"OFFICER ready ("<<offi_.svid<<")\n";
  }

  ~client()
  { std::cerr << "Client left " << addr << ":" << port << "\n"; //LESZEK
    free(buf);
    buf=NULL;
  }

  boost::asio::ip::tcp::socket& socket()
  { return socket_;
  }

  void start() //TODO consider providing a local user file pointer
  { addr = socket_.remote_endpoint().address().to_string();
    port = std::to_string(socket_.remote_endpoint().port());
    std::cerr << "Client entered " << addr << ":" << port << "\n";
    buf=(char*)std::malloc(txslen[TXSTYPE_MAX]+64+128);
    boost::asio::async_read(socket_,boost::asio::buffer(buf,1),
      boost::bind(&client::handle_read_txstype,shared_from_this(),boost::asio::placeholders::error));
    return;
  }

  void handle_read_txstype(const boost::system::error_code& error)
  { if(error){
      std::cerr<<"ERROR: read txstype error\n";
      offi_.leave(shared_from_this());
      return;}
    if(*buf>=TXSTYPE_MAX){
      std::cerr<<"ERROR: read txstype failed\n";
      offi_.leave(shared_from_this());
      return;}
    if(*buf==TXSTYPE_KEY){ // additional confirmation signature
      more=64;}
    len=txslen[(int)*buf]+64+more;
    //std::cerr << "Client txstype " << addr << ":" << port << "\n";
    boost::asio::async_read(socket_,boost::asio::buffer(buf+1,len-1),
      boost::bind(&client::handle_read_txs,shared_from_this(),boost::asio::placeholders::error));
    return;
  }

  void handle_read_txs(const boost::system::error_code& error)
  { if(error){
      std::cerr<<"ERROR: read txs error\n";
      offi_.leave(shared_from_this());
      return;}
    bzero(&utxs,sizeof(usertxs));
    utxs.parse(buf);
    utxs.print_head();
    if(*buf==TXSTYPE_BRO){
      buf=(char*)std::realloc(buf,len+utxs.bbank);
      //std::cerr << "Client more " << addr << ":" << port << "\n";
      boost::asio::async_read(socket_,boost::asio::buffer(buf+len,utxs.bbank),
        boost::bind(&client::handle_read_more,shared_from_this(),boost::asio::placeholders::error));
      return;}
    if(*buf==TXSTYPE_MPT){
      buf=(char*)std::realloc(buf,len+utxs.bbank*(6+8));
      //std::cerr << "Client more " << addr << ":" << port << "\n";
      boost::asio::async_read(socket_,boost::asio::buffer(buf+len,utxs.bbank*(6+8)),
        boost::bind(&client::handle_read_more,shared_from_this(),boost::asio::placeholders::error));
      return;}
    parse();
    offi_.leave(shared_from_this());
  }

  void handle_read_more(const boost::system::error_code& error)
  { if(error){
      std::cerr<<"ERROR: read more error\n";
      offi_.leave(shared_from_this());
      return;}
    parse();
    offi_.leave(shared_from_this());
  }

  void parse()
  { started=time(NULL);
    //std::cerr << "Client parse " << addr << ":" << port << "\n";
    uint32_t lpath=started-started%BLOCKSEC;
    uint32_t luser=0;
    uint16_t lnode=0;
    int64_t deposit=-1; // if deposit=0 inform target
    int64_t deduct=0;
    int64_t fee=0;
    user_t usera;
    int32_t diff=utxs.ttime-started;
    //TODO, should not allow same user multiple times in mpt otheriwse the log becomes unclear
    std::vector<uint16_t> mpt_bank; // for MPT to local bank
    std::vector<uint32_t> mpt_user; // for MPT to local bank
    std::vector< int64_t> mpt_mass; // for MPT to local bank
    msid=0;
    mpos=0;

    offi_.lock_user(utxs.auser); //needed to prevent 2 msgs from a user with same msid
//FIXME, read the rest ... add additional signatures
    //consider adding a max txs limit per user
    if(!offi_.get_user(usera,utxs.abank,utxs.auser)){
      std::cerr<<"ERROR: read user failed\n";
      offi_.unlock_user(utxs.auser);
      return;}
    if(utxs.wrong_sig((uint8_t*)buf,(uint8_t*)usera.hash,(uint8_t*)usera.pkey)){
      std::cerr<<"ERROR: bad signature\n";
      offi_.unlock_user(utxs.auser);
      return;}
    if(*buf==TXSTYPE_KEY && utxs.wrong_sig2((uint8_t*)buf)){
      std::cerr<<"ERROR: bad second signature\n";
      offi_.unlock_user(utxs.auser);
      return;}
    if(diff>2 && *buf!=TXSTYPE_BLG){
      std::cerr<<"ERROR: time in the future ("<<diff<<">2s)\n";
      offi_.unlock_user(utxs.auser);
      return;}
    if(*buf==TXSTYPE_BKY){
      hash_t skey;
      if(!offi_.find_key((uint8_t*)utxs.key(buf),skey)){
        std::cerr<<"ERROR: bad second signature\n";
        offi_.unlock_user(utxs.auser);
        return;}}
    if(*buf==TXSTYPE_INF){ // this is special, just local info
      if((abs(diff)>2)){
        std::cerr<<"ERROR: high time difference ("<<diff<<">2s)\n";
        offi_.unlock_user(utxs.auser);
        return;}
//FIXME, read data also from server
//FIXME, if local account locked, check if unlock was successfull based on time passed after change
      if(utxs.abank!=offi_.svid && utxs.bbank!=offi_.svid){
        std::cerr<<"ERROR: bad bank for INF\n";
        offi_.unlock_user(utxs.auser);
        return;}
      std::cerr<<"SENDING user info ("<<utxs.bbank<<":"<<utxs.buser<<")\n";
      user_t userb;
      if(utxs.abank!=utxs.bbank || utxs.auser!=utxs.buser){
        if(!offi_.get_user(userb,utxs.bbank,utxs.buser)){
          std::cerr<<"FAILED to get local user info ("<<utxs.bbank<<":"<<utxs.buser<<")\n";
          offi_.unlock_user(utxs.auser);
          return;}
        boost::asio::write(socket_,boost::asio::buffer(&userb,sizeof(user_t)));}
      else{
        boost::asio::write(socket_,boost::asio::buffer(&usera,sizeof(user_t)));}
      if(!offi_.get_user_global(userb,utxs.bbank,utxs.buser)){
        std::cerr<<"FAILED to get global user info ("<<utxs.bbank<<":"<<utxs.buser<<")\n";
        offi_.unlock_user(utxs.auser);
        return;}
      boost::asio::write(socket_,boost::asio::buffer(&userb,sizeof(user_t)));
      offi_.unlock_user(utxs.auser);
      return;}
    //if(utxs.abank!=offi_.svid && *buf!=TXSTYPE_USR){
    if(utxs.abank!=offi_.svid){
      std::cerr<<"ERROR: bad bank\n";
      offi_.unlock_user(utxs.auser);
      return;}
    if(*buf==TXSTYPE_LOG){
      //if(utxs.abank!=offi_.svid){
      //  std::cerr<<"ERROR: bad bank for LOG\n";
      //  return;}
      boost::asio::write(socket_,boost::asio::buffer(&usera,sizeof(user_t)));
      std::string slog;
      if(!offi_.get_log(utxs.abank,utxs.auser,utxs.ttime,slog)){
        std::cerr<<"ERROR: get log failed\n";
        offi_.unlock_user(utxs.auser);
        return;}
      LOG("SENDING user %04X:%08X log [size:%lu]\n",utxs.abank,utxs.auser,slog.size());
      boost::asio::write(socket_,boost::asio::buffer(slog.c_str(),slog.size()));
      offi_.unlock_user(utxs.auser);
      return;}
    if(*buf==TXSTYPE_BLG){
      uint32_t head[2];
      uint32_t &path=head[0];
      uint32_t &size=head[1];
      uint32_t lpath=offi_.last_path();
      if(!utxs.ttime){
        path=lpath;}
      else{
        path=utxs.ttime-utxs.ttime%BLOCKSEC;
        if(path>=lpath){
          LOG("ERROR, broadcast %08X not ready (>=%08X)\n",path,lpath);
          offi_.unlock_user(utxs.auser);
          return;}}
      //FIXME, report only completed broadcast files (<=last_path())
      char filename[64];
      sprintf(filename,"blk/%03X/%05X/bro.log",path>>20,path&0xFFFFF);
      int fd=open(filename,O_RDONLY);
      if(fd<0){
        size=0;
        boost::asio::write(socket_,boost::asio::buffer(head,2*sizeof(uint32_t)));
        LOG("SENDING broadcast log %08X [empty]\n",path);
        offi_.unlock_user(utxs.auser);
        return;}
      struct stat sb;
      fstat(fd,&sb);
      if(sb.st_size>MAX_BLG_SIZE){
        sb.st_size=MAX_BLG_SIZE;}
      if(sb.st_size>MESSAGE_CHUNK){ // change to MESSAGE_TOO_LONG or similar
        size=MESSAGE_CHUNK;}
      else{
        size=sb.st_size;}
      LOG("SENDING broadcast log %08X [len:%d]\n",path,size);
      if(!size){
        boost::asio::write(socket_,boost::asio::buffer(head,2*sizeof(uint32_t)));
        close(fd);
        offi_.unlock_user(utxs.auser);
        return;}
      char buf[2*sizeof(uint32_t)+size];
      memcpy(buf,head,2*sizeof(uint32_t));
      for(uint32_t pos=0;pos<sb.st_size;){
        int len=read(fd,buf+2*sizeof(uint32_t),size);
        if(len<=0){
          close(fd);
          LOG("ERROR, failed to read BROADCAST LOG %s [size:%08X,pos:%08X]\n",filename,size,pos);
          offi_.unlock_user(utxs.auser);
          return;}
        if(!pos){
          boost::asio::write(socket_,boost::asio::buffer(buf,2*sizeof(uint32_t)+len));}
        else{
          boost::asio::write(socket_,boost::asio::buffer(buf+2*sizeof(uint32_t),len));}
        pos+=len;}
      close(fd);
      offi_.unlock_user(utxs.auser);
      return;}

    if(*buf==TXSTYPE_BLK){
      offi_.unlock_user(utxs.auser); //FIXME, unlock before sending output (fix other commands)
      servers block;
      uint32_t from=utxs.amsid-utxs.amsid%BLOCKSEC; //TODO replace 0 by the available beginning or last checkpoint
      uint32_t to=utxs.buser-utxs.buser%BLOCKSEC;
      uint32_t start=block.read_start();
      int vip_tot=0;
      if(!start){
        LOG("ERROR, failed to read block start\n");
        return;}
      header_t head;
      bzero(head.viphash,32);
      if(!from){
        from=start;
        block.now=from;
        if(block.header_get()){
          block.header(head);}
        else{
          LOG("ERROR, failed to read block at start %08X\n",start);
          return;}
        //start with first viphash
        int len=0;
        char* data=NULL;
        block.load_vip(len,data,block.viphash);
        if(!len){
          char hash[65]; hash[64]='\0';
          ed25519_key2text(hash,block.viphash,32);
          LOG("ERROR, failed to provide vip keys for start viphash %.64s\n",hash);
          return;}
        vip_tot=len/(2+32);
        if(!block.vip_check(block.viphash,(uint8_t*)data+4,len/(2+32))){
          char hash[65]; hash[64]='\0';
          ed25519_key2text(hash,block.viphash,32);
          LOG("ERROR, failed to provide %d correct vip keys for start viphash %.64s\n",len/(2+32),hash);
          free(data);
          return;}
        else{
          char hash[65]; hash[64]='\0';
          ed25519_key2text(hash,block.viphash,32);
          LOG("INFO, sending vip keys for start viphash %.64s [len:%d]\n",hash,len);}
        boost::asio::write(socket_,boost::asio::buffer(data,len+4));
        free(data);}
      else if(from<start){
        LOG("ERROR, failed to read block %08X before start %08X\n",from,start);
        return;}
      else{
        block.now=from-BLOCKSEC;
        if(block.header_get()){
          vip_tot=block.vip_size(block.viphash);
          block.header(head);}}
      if(!to){
        to=time(NULL);
        to-=to%BLOCKSEC-BLOCKSEC;}
      if(from>to){
        LOG("ERROR, no block between %08X and %08X\n",from,to);
        return;}
      std::string blocksstr;
      uint32_t n=0;
      blocksstr.append((const char*)&n,4);
      bool newviphash=false;
      uint32_t trystop=from+128;
      for(block.now=from;block.now<=to;){
        if(!block.header_get()){
          //LOG("ERROR, failed to provide block %08X\n",block.now);
          break;}
        if(memcmp(head.viphash,block.viphash,32)){
          newviphash=true;
          to=block.now;}
        block.header(head);
        LOG("INFO, adding block %08X\n",block.now);
        blocksstr.append((const char*)&head,sizeof(header_t));
	block.now+=BLOCKSEC;
        //FIXME, user different stop criterion !!! do not stop if there are not enough signatures
        n++;
        //if(!(block.now%(BLOCKDIV*BLOCKSEC))){
        if(block.now>trystop && 2*block.vok>=vip_tot){
          break;}}
      block.now-=BLOCKSEC;
      blocksstr.replace(0,4,(const char*)&n,4);
      if(!n){
        LOG("ERROR, failed to provide blocks from %08X to %08X\n",from,block.now);
        return;}
      else{
        LOG("SEND %d blocks from %08X to %08X\n",(int)*((int*)blocksstr.c_str()),from,block.now);}
      boost::asio::write(socket_,boost::asio::buffer(blocksstr.c_str(),4+n*sizeof(header_t)));
      //always send signatures for last block
      uint8_t *data=NULL;
      uint32_t nok;
      if(block.get_signatures(block.now,data,nok)){
        LOG("INFO, sending %d signatures for block %08X\n",nok,block.now);
        boost::asio::write(socket_,boost::asio::buffer(data,8+nok*sizeof(svsi_t)));}
      else{ //else send 0
        //FIXME, go back and find a block with enough signatures
        LOG("ERROR, sending no signatures for block %08X\n",block.now);
        uint64_t zero=0;
        boost::asio::write(socket_,boost::asio::buffer(&zero,8));}
      free(data);
      //send new viphash if detected
      if(newviphash){
        int len=0;
        char* data=NULL;
        block.load_vip(len,data,block.viphash);
        if(!len){
          char hash[65]; hash[64]='\0';
          ed25519_key2text(hash,block.viphash,32);
          LOG("ERROR, failed to provide vip keys for start viphash %.64s\n",hash);
          return;}
        if(!block.vip_check(block.viphash,(uint8_t*)data+4,len/(2+32))){
          char hash[65]; hash[64]='\0';
          ed25519_key2text(hash,block.viphash,32);
          LOG("ERROR, failed to provide %d correct vip keys for start viphash %.64s\n",len/(2+32),hash);
          free(data);
          return;}
        boost::asio::write(socket_,boost::asio::buffer(data,4+len));
        free(data);}
      else{ //else send 0
        uint32_t zero=0;
        boost::asio::write(socket_,boost::asio::buffer(&zero,4));}
      return;}

    if(*buf==TXSTYPE_TXS){

      offi_.unlock_user(utxs.auser);
      return;}

    if(*buf==TXSTYPE_VIP){
      int len=0;
      char* buf=NULL;
      servers srvs_;
      srvs_.load_vip(len,buf,utxs.tinfo);
      if(!len){
        char hash[65]; hash[64]='\0';
        ed25519_key2text(hash,srvs_.viphash,32);
        LOG("ERROR, failed to provide vip keys %.64s\n",hash);
        offi_.unlock_user(utxs.auser);
        return;}
      boost::asio::write(socket_,boost::asio::buffer(buf,len+4));
      free(buf);
      offi_.unlock_user(utxs.auser);
      return;}

    if(*buf==TXSTYPE_SIG){

      offi_.unlock_user(utxs.auser);
      return;}

    if(usera.msid!=utxs.amsid){
      std::cerr<<"ERROR: bad msid ("<<usera.msid<<"<>"<<utxs.amsid<<")\n";
      offi_.unlock_user(utxs.auser);
      return;}
    //if(usera.time>utxs.ttime){ //does not work because of _GET
    //  std::cerr<<"ERROR: bad transaction time ("<<usera.time<<">"<<utxs.ttime<<")\n";
    //  return;}
    //include additional 2FA later

    //commit trasaction
    if(usera.time+LOCK_TIME<lpath && usera.user && usera.node && (usera.user!=utxs.auser || usera.node!=utxs.abank)){//check account lock
      if(*buf!=TXSTYPE_PUT || utxs.abank!=utxs.bbank || utxs.auser!=utxs.buser || utxs.tmass!=0){
        std::cerr<<"ERROR: account locked, send 0 to yourself and wait for unlock\n";
        offi_.unlock_user(utxs.auser);
        return;}
      if(usera.lpath>usera.time){
        std::cerr<<"ERROR: account unlock in porgress\n";
        offi_.unlock_user(utxs.auser);
        return;}
      utxs.ttime=usera.time;//lock time not changed
      //need to read data with _INF to confirm unlock !!!
      luser=usera.user;
      lnode=usera.node;}
    else if(*buf==TXSTYPE_BRO){
      fee=TXS_BRO_FEE(utxs.bbank);}
    else if(*buf==TXSTYPE_PUT){
      if(utxs.tmass<0){ //sending info about negative values is allowed to fascilitate exchanges
        utxs.tmass=0;}
      //if(utxs.abank!=utxs.bbank && utxs.auser!=utxs.buser && !check_user(utxs.bbank,utxs.buser)){
      if(!offi_.check_user(utxs.bbank,utxs.buser)){
        // does not check if account closed [consider adding this slow check]
	std::cerr<<"ERROR: bad target user ("<<utxs.bbank<<":"<<utxs.buser<<")\n";
        offi_.unlock_user(utxs.auser);
        return;}
      deposit=utxs.tmass;
      deduct=utxs.tmass;
      fee=TXS_PUT_FEE(utxs.tmass);
      if(utxs.abank!=utxs.bbank){
        fee+=TXS_LNG_FEE(utxs.tmass);}}
    else if(*buf==TXSTYPE_MPT){
      char* tbuf=utxs.toaddresses(buf);
      //utxs.print_toaddresses(buf,utxs.bbank);
      utxs.tmass=0;
      mpt_bank.reserve(utxs.bbank);
      mpt_user.reserve(utxs.bbank);
      mpt_mass.reserve(utxs.bbank);
      std::set<uint64_t> out;
      union {uint64_t big;uint32_t small[2];} to;
      to.small[1]=0;
      fee=TXS_MIN_FEE;
      for(int i=0;i<utxs.bbank;i++,tbuf+=6+8){
        uint32_t& tuser=to.small[0];
        uint32_t& tbank=to.small[1];
        //uint16_t tbank;
        //uint32_t tuser;
         int64_t tmass;
        memcpy(&tbank,tbuf+0,2);
        memcpy(&tuser,tbuf+2,4);
        memcpy(&tmass,tbuf+6,8);
        if(tmass<=0){ //only positive non-zero values allowed
          std::cerr<<"ERROR: only positive non-zero transactions allowed in MPT\n";
          offi_.unlock_user(utxs.auser);
          return;}
        if(out.find(to.big)!=out.end()){
          LOG("ERROR: duplicate target: %04X:%08X\n",tbank,tuser);
          offi_.unlock_user(utxs.auser);
          return;}
        if(!offi_.check_user((uint16_t)tbank,tuser)){
          // does not check if account closed [consider adding this slow check]
          std::cerr<<"ERROR: bad target user ("<<tbank<<":"<<tuser<<")\n";
          offi_.unlock_user(utxs.auser);
          return;}
        out.insert(to.big);
        mpt_bank.push_back((uint16_t)tbank);
        mpt_user.push_back(tuser);
        mpt_mass.push_back(tmass);
        fee+=TXS_MPT_FEE(tmass);
        if(utxs.abank!=tbank){
          fee+=TXS_LNG_FEE(tmass);}
        utxs.tmass+=tmass;}
      deposit=utxs.tmass;
      deduct=utxs.tmass;}
    else if(*buf==TXSTYPE_USR){
      //if(utxs.bbank!=offi_.svid){
      //  std::cerr<<"ERROR: bad target bank ("<<utxs.bbank<<")\n";
      //  offi_.unlock_user(utxs.auser);
      //  return;}
      deduct=USER_MIN_MASS;
      if(utxs.abank!=utxs.bbank){
        fee=TXS_USR_FEE;}
      else{
        fee=TXS_MIN_FEE;}
      if(deduct+fee+USER_MIN_MASS>usera.weight){ //check in advance before creating new user
        std::cerr<<"ERROR: too low balance ("<<deduct<<"+"<<fee<<"+"<<USER_MIN_MASS<<">"<<usera.weight<<")\n";
        offi_.unlock_user(utxs.auser);
        return;}
      if(utxs.bbank!=offi_.svid){
        uint32_t now=time(NULL);
        if(now%BLOCKSEC>BLOCKSEC/2){
          LOG("ERROR: bad timing for remote account request, try after %d seconds\n",
            BLOCKSEC-now%BLOCKSEC);
          offi_.unlock_user(utxs.auser);
          return;}
        bzero(buf+txslen[(int)*buf]+64+0,4+32);
        utxs.buser=0;} //FIXME, remove 32 bytes
      else{
        uint32_t nuser=offi_.add_user(utxs.abank,usera.pkey,utxs.ttime,utxs.auser);
        if(!nuser){
          std::cerr<<"ERROR: failed to open account\n";
          offi_.unlock_user(utxs.auser);
          return;}
        memcpy(buf+txslen[(int)*buf]+64+0,&nuser,4);
        memcpy(buf+txslen[(int)*buf]+64+4,usera.pkey,32); //FIXME, this data is not needed !!!
        lnode=0;
        luser=nuser;
	utxs.buser=nuser;}}
      /*if(utxs.abank!=offi_.svid){
      //if(utxs.abank!=offi_.svid && !offi_.try_account((hash_s*)usera.pkey)){
      //  std::cerr<<"ERROR: failed to open account (pkey known)\n";
      //  offi_.unlock_user(utxs.auser);
      //  return;}
      // add_account ...

        //FIXME, check if original bank is in our whitelist
        offi_.add_msg((uint8_t*)buf,utxs.size,msid,mpos); //TODO, could return pointer to file
        offi_.add_account((hash_s*)usera.pkey,nuser); //blacklist
        user_t userb;
        offi_.get_user(userb,utxs.bbank,nuser); // send userb
//FIXME, send also transaction info
        boost::asio::write(socket_,boost::asio::buffer(&userb,sizeof(user_t)));
        offi_.unlock_user(utxs.auser);
        return;}}*/
    else if(*buf==TXSTYPE_BNK){ // we will get a confirmation from the network
      deduct=BANK_MIN_TMASS;
      fee=TXS_BNK_FEE;}
    else if(*buf==TXSTYPE_GET){ // we will get a confirmation from the network
      if(utxs.abank==utxs.bbank){
	std::cerr<<"ERROR: bad bank ("<<utxs.bbank<<"), use PUT\n";
        offi_.unlock_user(utxs.auser);
        return;}
//FIXME, check second user and stop transaction if GET is pednig or there are no funds
      fee=TXS_GET_FEE;}
    else if(*buf==TXSTYPE_KEY){
      memcpy(usera.pkey,utxs.key(buf),32);
      fee=TXS_KEY_FEE;}
    else if(*buf==TXSTYPE_BKY){ // we will get a confirmation from the network
      if(utxs.auser){
	std::cerr<<"ERROR: bad user ("<<utxs.auser<<") for this bank changes\n";
        offi_.unlock_user(utxs.auser);
        return;}
      //buf=(char*)std::realloc(buf,utxs.size);
      fee=TXS_BKY_FEE;}
    //else if(*buf==TXSTYPE_STP){ // we will get a confirmation from the network
    //  assert(0); //TODO, not implemented later
    //  fee=TXS_STP_FEE;}
    if(deduct+fee+(utxs.auser?USER_MIN_MASS:BANK_MIN_UMASS)>usera.weight){
      LOG("ERROR: too low balance txs:%016lX+fee:%016lX+min:%016lX>now:%016lX\n",
        deduct,fee,(uint64_t)(utxs.auser?USER_MIN_MASS:BANK_MIN_UMASS),usera.weight);
      offi_.unlock_user(utxs.auser);
      return;}
    //send message
    //commit bank key change
    if(*buf==TXSTYPE_BKY){ // commit key change
      memcpy(utxs.opkey(buf),offi_.pkey,32);
      memcpy(offi_.pkey,utxs.key(buf),32);}
    offi_.add_msg((uint8_t*)buf,utxs.size,msid,mpos);
    if(!msid||!mpos){
      std::cerr<<"ERROR: message submission failed ("<<msid<<","<<mpos<<")\n";
      offi_.unlock_user(utxs.auser);
      return;}
    //commit changes
    if(*buf!=TXSTYPE_PUT){ // store additional info in log
      // this is ok for MPT
      memcpy(utxs.tinfo+ 0,&usera.weight,8);
      memcpy(utxs.tinfo+ 8,&deduct,8);
      memcpy(utxs.tinfo+16,&fee,8);
      memcpy(utxs.tinfo+24,&usera.stat,2);
      memcpy(utxs.tinfo+26,&usera.pkey,6);}
    usera.msid++;
    usera.time=utxs.ttime;
    usera.node=lnode;
    usera.user=luser;
    usera.lpath=lpath;
    //convert message to hash (use signature as input)
    uint8_t hash[32];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256,utxs.get_sig(buf),64);
    SHA256_Final(hash,&sha256);
    //make newhash=hash(oldhash+newmessagehash);
    SHA256_Init(&sha256);
    SHA256_Update(&sha256,usera.hash,32);
    SHA256_Update(&sha256,hash,32);
    SHA256_Final(usera.hash,&sha256);
    offi_.set_user(utxs.auser,usera,deduct+fee);
    //log
    log_t tlog;
    tlog.time=time(NULL);
    tlog.type=*buf;
    tlog.node=utxs.bbank;
    tlog.user=utxs.buser;
    tlog.umid=utxs.amsid;
    tlog.nmid=msid;
    tlog.mpos=mpos;
    memcpy(tlog.info,utxs.tinfo,32);
    if(*buf==TXSTYPE_MPT && mpt_user.size()>0){
      int end=mpt_user.size();
      std::map<uint64_t,log_t> log;
      for(int i=0;i<end;i++){
        uint64_t key=((uint64_t)utxs.auser)<<32;
        key|=i;
        tlog.node=mpt_bank[i];
        tlog.user=mpt_user[i];
        tlog.weight=-mpt_mass[i];
        tlog.info[31]=(i?0:1);
	log[key]=tlog;}
      offi_.put_ulog(log);} // could be processed 
    else{
      //tlog.weight=-utxs.tmass;
      tlog.weight=-deduct; // includes deducts in TXSTYPE_BNK TXSTYPE_USR
      offi_.put_ulog(utxs.auser,tlog);}

    if(*buf==TXSTYPE_MPT && mpt_user.size()>0){
      tlog.type=*buf|0x8000; //incoming
      tlog.node=utxs.abank;
      tlog.user=utxs.auser;
      int end=mpt_user.size();
      for(int i=0;i<end;i++){
        if(mpt_bank[i]==offi_.svid){
          tlog.weight=mpt_mass[i];
          tlog.info[31]=(i?0:1);
          //offi_.put_ulog(utxs.buser,tlog);
          offi_.put_ulog(mpt_user[i],tlog);
          if(mpt_mass[i]>=0){
            offi_.add_deposit(mpt_user[i],mpt_mass[i]);}}}}
    else if(utxs.abank==utxs.bbank && (*buf==TXSTYPE_PUT || (*buf==TXSTYPE_USR && utxs.buser))){
      tlog.type=*buf|0x8000; //incoming
      tlog.node=utxs.abank;
      tlog.user=utxs.auser;
      tlog.weight=deduct; //utxs.tmass;
      offi_.put_ulog(utxs.buser,tlog);
      if(*buf==TXSTYPE_PUT && deposit>=0){
        offi_.add_deposit(utxs);}}
    offi_.unlock_user(utxs.auser);
    //FIXME, return msid and mpos
    LOG("SENDING new user info %04X:%08X @ msg %08X:%08X\n",utxs.abank,utxs.auser,msid,mpos);
    try{
      //respond with a single message
      boost::asio::write(socket_,boost::asio::buffer(&usera,sizeof(user_t))); //consider signing this message
      boost::asio::write(socket_,boost::asio::buffer(&msid,sizeof(uint32_t)));
      boost::asio::write(socket_,boost::asio::buffer(&mpos,sizeof(uint32_t)));}
    catch (std::exception& e){
      LOG("ERROR responding to client %08X\n",utxs.auser);}
    return;
  }

  uint32_t started; //time started
private:
  boost::asio::ip::tcp::socket socket_;
  office& offi_;
  std::string addr;
  std::string port;
  char* buf;
  int len;
  int more;
  usertxs utxs;
  uint32_t msid;
  uint32_t mpos;
};

#endif // CLIENT_HPP
