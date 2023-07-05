#include "Writer.h"
#include "debug.h"
#include "unistd.h"
#include "HecubaExtrae.h"
#include "WriterThread.h"



Writer::Writer(const TableMetadata *table_meta, CassSession *session,
               std::map<std::string, std::string> &config) {


    DBG( " WRITER: Constructor for "<<table_meta->keyspace<<"."<<table_meta->table" @"<< this);
    this->disable_timestamps = false;

    if (config.find("timestamped_writes") != config.end()) {
        std::string check_timestamps = config["timestamped_writes"];
        std::transform(check_timestamps.begin(), check_timestamps.end(), check_timestamps.begin(), ::tolower);
        if (check_timestamps == "false" || check_timestamps == "no")
            disable_timestamps = true;
    }

    this->session = session;
    this->table_metadata = table_meta;
    this->k_factory = new TupleRowFactory(table_meta->get_keys());
    this->v_factory = new TupleRowFactory(table_meta->get_values());

    HecubaExtrae_event(HECUBACASS, HBCASS_PREPARES);
    CassFuture *future = cass_session_prepare(session, table_meta->get_insert_query());
    CassError rc = cass_future_error_code(future);
    CHECK_CASS("writer cannot prepare: ");
    this->prepared_query = cass_future_get_prepared(future);
    cass_future_free(future);

    // Prepare partial queries for all values

    for (auto cm: *(table_meta->get_values()) ) {
        const char* insert_q = table_metadata->get_partial_insert_query(cm.info["name"]);
        CassFuture *future = nullptr;
        try {
            future = cass_session_prepare(session, insert_q);
        } catch (std::exception &e) {
            std::string msg(e.what());
            msg += " Problem in execute " + std::string(insert_q);
            throw ModuleException(msg);
        }
        CassError rc = cass_future_error_code(future);
        CHECK_CASS("writer cannot prepare: ");
        prepared_partial_queries[cm.info["name"]] = cass_future_get_prepared(future);
    }
    HecubaExtrae_event(HECUBACASS, HBCASS_END);
    this->ncallbacks = 0;
    this->timestamp_gen = new TimestampGenerator();
    this->lazy_write_enabled = false; // Disabled by default, will be enabled on ArrayDataStore
    this->dirty_blocks = new tbb::concurrent_hash_map <const TupleRow *, const TupleRow *, Writer::HashCompare >();
    this->topic_name = nullptr;
    this->topic = nullptr;
    this->producer = nullptr;
    myconfig = &config;
}

Writer::Writer(const Writer&  src) {
    *this = src;
}

Writer& Writer::operator = (const Writer& src) {
    this->disable_timestamps = src.disable_timestamps;
    this->session = src.session; // CassSession is not deleted in the Writer destructor
    this->table_metadata = src.table_metadata; // TableMetadata is not deleted in the Writer destructor
    if (this->k_factory != nullptr) { delete(this->k_factory); }
    if (this->v_factory != nullptr) { delete(this->v_factory); }
    this->k_factory = new TupleRowFactory(src.table_metadata->get_keys());
    this->v_factory = new TupleRowFactory(src.table_metadata->get_values());

    CassFuture *future = cass_session_prepare(session, src.table_metadata->get_insert_query());
    CassError rc = cass_future_error_code(future);
    CHECK_CASS("writer cannot prepare: ");
    this->prepared_query = cass_future_get_prepared(future);
    cass_future_free(future);
    // if we copy the writer we copy the characteristics of the writer but we do not inherit the pending writes: we initialize both dirty_blocks and data, and we set to 0 the number of callbacks
    //this->data = src.data; // concurrent_bounded_queue implements copy assignment: this does not compile because concurrent bounded queue implements move assignment
    //this->dirty_blocks = src.dirty_blocks; //concurrent_hash_map implements copy assignment
    if (this->dirty_blocks != nullptr) { delete (this->dirty_blocks); }
    this->dirty_blocks = new tbb::concurrent_hash_map <const TupleRow *, const TupleRow *, Writer::HashCompare >();
    this->ncallbacks = 0;
    this->max_calls = src.max_calls;
    if (this->timestamp_gen != nullptr) { delete (this->timestamp_gen); }
    this->timestamp_gen = new TimestampGenerator();; // TimestampGenerator has a class attribute of type mutex which is not copy-assignable
    this->lazy_write_enabled = src.lazy_write_enabled;

    //kafka is plain c code, it does not implement copy assignment semantic
    if (this->topic_name != nullptr){ free(this->topic_name); }
    if (src.topic_name != nullptr) {
        this->topic_name = (char *) malloc (strlen(src.topic_name)+1);
        strcpy(this->topic_name, src.topic_name);
        this->producer = src.producer; //TODO this should be shared pointer or create a new producer
        this->topic = src.topic; // TODO this should be shared pointer or create a new topic
    } else {
        this->topic_name = nullptr;
        this->topic = nullptr;
        this->producer = nullptr;
    }
    return *this;
}

Writer::~Writer() {
    DBG( " WRITER: Destructor "<< ((topic_name!=nullptr)?topic_name:""));
    wait_writes_completion(); // WARNING! It is necessary to wait for ALL CALLBACKS to finish, because the 'data' structure required by the callback will dissapear with this destructor
    //std::cout<< " WRITER: Finished thread "<< async_query_thread_id << std::endl;
    if (this->prepared_query != NULL) {
        cass_prepared_free(this->prepared_query);
        prepared_query = NULL;
    }
    for(auto it: prepared_partial_queries) {
        cass_prepared_free(it.second);
        it.second = nullptr;
    }

    if (this->topic_name) {
        free(this->topic_name);
        this->topic_name = NULL;
        rd_kafka_topic_destroy(this->topic);
        this->topic = NULL;
        rd_kafka_resp_err_t err;
        do {
            err = rd_kafka_flush(this->producer, 500);
        } while(err == RD_KAFKA_RESP_ERR__TIMED_OUT);
        rd_kafka_destroy(this->producer);
        this->producer = NULL;
    }
    delete (this->k_factory);
    delete (this->v_factory);
    delete (this->timestamp_gen);
    delete (this->dirty_blocks);
    // table_metadata NOT FREED (Shared?)
    // session NOT FREED (Shared?)
}


rd_kafka_conf_t * Writer::create_stream_conf(std::map<std::string,std::string> &config){
    char errstr[512];
    char hostname[128];
    rd_kafka_conf_t *conf = rd_kafka_conf_new();

    if (gethostname(hostname, sizeof(hostname))) {
        fprintf(stderr, "%% Failed to lookup hostname\n");
        exit(1);
    }

    // PRODUCER: Why do we need to set client.id????
    if (rd_kafka_conf_set(conf, "client.id", hostname,
                              errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
        fprintf(stderr, "%% %s\n", errstr);
        exit(1);
    }

    // CONSUMER: Why do we need to set group.id????
    if (rd_kafka_conf_set(conf, "group.id", "hecuba",
                errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
        fprintf(stderr, "%% %s\n", errstr);
        exit(1);
    }

    // Setting bootstrap.servers...
    if (config.find("kafka_names") == config.end()) {
        throw ModuleException("KAFKA_NAMES are not set. Use: 'host1:9092,host2:9092'");
    }
    std::string kafka_names = config["kafka_names"];

    if (rd_kafka_conf_set(conf, "bootstrap.servers", kafka_names.c_str(),
                              errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
        fprintf(stderr, "%% %s\n", errstr);
        exit(1);
    }
    return conf;
}



void Writer::enable_stream(const char* topic_name, std::map<std::string, std::string> &config) {
    if (this->topic_name != nullptr) {
        throw ModuleException(" Ooops. Stream already initialized.Trying to initialize "+std::string(this->topic_name)+" with "+std::string(topic_name));
    }

    DBG("Writer::enable_stream with topic ["<<topic_name<<"]");

    rd_kafka_conf_t * conf = create_stream_conf(config);


    this->topic_name = (char *) malloc(strlen(topic_name) + 1);
    strcpy(this->topic_name, topic_name);

    char errstr[512];

    /* Create Kafka producer handle */
    rd_kafka_t *rk;
    if (!(rk = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr)))) {
          fprintf(stderr, "%% Failed to create new producer: %s\n", errstr);
            exit(1);
    }

    // Create topic
    rd_kafka_topic_t *rkt = rd_kafka_topic_new(rk, topic_name, NULL);

    this->producer = rk;
    this->topic = rkt;
}

void Writer::send_event(char* event, const uint64_t size) {
    if (this->topic_name == nullptr) {
        throw ModuleException(" Ooops. Stream is not initialized");
    }
	rd_kafka_resp_err_t err;
	err = rd_kafka_producev(
                    /* Producer handle */
                    this->producer,
                    /* Topic name */
                    RD_KAFKA_V_TOPIC(this->topic_name),
                    /* Make a copy of the payload. */
                    RD_KAFKA_V_MSGFLAGS(RD_KAFKA_MSG_F_COPY),
                    /* Message value and length */
                    RD_KAFKA_V_VALUE(event, size),
                    /* Per-Message opaque, provided in
                     * delivery report callback as
                     * msg_opaque. */
                    RD_KAFKA_V_OPAQUE(NULL),
                    /* End sentinel */
                    RD_KAFKA_V_END);
	if (err) {
        char b[256];
        sprintf(b, "%% Failed to produce to topic %s: %s\n",
                this->topic_name, rd_kafka_err2str(rd_kafka_errno2err(errno)));
        throw ModuleException(b);
	}

}

void Writer::send_event(const TupleRow* key, const TupleRow *value) {
    std::vector <uint32_t> key_sizes = this->k_factory->get_content_sizes(key);
    std::vector <uint32_t> value_sizes = this->v_factory->get_content_sizes(value);

    size_t row_size=0;
    size_t key_size=0;
    for (auto&elt: key_sizes) {
        key_size += elt;
    }
    row_size=key_size;
    for (auto&elt: value_sizes) {
        row_size += elt;
    }
    uint32_t keynullvalues_size = std::ceil(((double)key->n_elem())/32)*sizeof(uint32_t);
    uint32_t valuenullvalues_size = std::ceil(((double)value->n_elem())/32)*sizeof(uint32_t);

    row_size += keynullvalues_size + valuenullvalues_size;

    char *rowpayload = (char *) malloc(row_size);

    this->k_factory->encode(key, rowpayload);
    this->v_factory->encode(value, rowpayload+key_size+keynullvalues_size);

    this->send_event(rowpayload, row_size);

    // REMOVE ME //fprintf(stderr, "Send event to topic %s\n", this->topic_name);
    // REMOVE ME bool is_all_null=true;
    // REMOVE ME for (uint32_t i=0; i< key->n_elem(); i++) {
    // REMOVE ME      if (!key->isNull(i)) {
    // REMOVE ME         is_all_null=false;
    // REMOVE ME      }
    // REMOVE ME }
    // REMOVE ME if (!is_all_null) {
    // REMOVE ME     this->write_to_cassandra(key, value); // Write key,value to cassandra only if the key is not null
    // REMOVE ME }

}
/* send_event: Send and Store a WHOLE ROW in CASSANDRA */
void Writer::send_event(void* key, void* value) {
    const TupleRow *k = k_factory->make_tuple(key);
    const TupleRow *v = v_factory->make_tuple(value);
    this->send_event(k, v);
    delete(k);
    delete(v);
}

#if 0
/* TODO: complete this code for storageObj implementation */

/* send_event: Send and Store a SINGLE COLUMN in CASSANDRA */
void Writer::send_event(void* key, void* value, char* attr_name) {

    TupleRowFactory * v_single_factory = new TupleRowFactory(table_metadata->get_single_value(attr_name));
    const TupleRow *k = k_factory->make_tuple(key);
    const TupleRow *v = v_single_factory->make_tuple(value);

    this->send_event(k, v);
    delete(v_single_factory);
    delete(k);
    delete(v);
}
#endif

void Writer::set_timestamp_gen(TimestampGenerator *time_gen) {
    delete(this->timestamp_gen);
    this->timestamp_gen = time_gen;
}


void Writer::finish_async_call() {
    ncallbacks--;
}
void Writer::flush_dirty_blocks() {
    //std::cout<< "Writer::flush_dirty_blocks "<<std::endl;
    int n = 0;
    for( auto x = dirty_blocks->begin(); x != dirty_blocks->end(); x++) {
        //std::cout<< "  Writer::flushing item "<<std::endl;
        n ++;
        queue_async_query(x->first, x->second);
        delete(x->first);
        delete(x->second);
    }
    dirty_blocks->clear();
    //std::cout<< "Writer::flush_dirty_blocks "<< n << " blocks FLUSHED"<<std::endl;
}

// flush all the pending write requests: send them to Cassandra driver and wait for finalization (called from outside)
void Writer::flush_elements() {
    wait_writes_completion();
}

bool Writer::is_write_completed() const {
    return ( (ncallbacks == 0) && dirty_blocks->empty() );
}

// wait for callbacks execution for all sent write requests
void Writer::wait_writes_completion(void) {
    HecubaExtrae_event(HECUBADBG, HECUBA_FLUSHELEMENTS);
    flush_dirty_blocks();
    //std::cout<< "Writer::wait_writes_completion * Waiting for "<< data.size() << " Pending "<<ncallbacks<<" callbacks" <<" inflight"<<std::endl;
    while( ! is_write_completed() ) {
        std::this_thread::yield();
    }
    HecubaExtrae_event(HECUBADBG, HECUBA_END);
    //std::cout<< "Writer::wait_writes_completion2* Waiting for "<< data.size() << " Pending "<<ncallbacks<<" callbacks" <<" inflight"<<std::endl;
}





void Writer::enable_lazy_write(void) {
    this->lazy_write_enabled = true;
}

void Writer::disable_lazy_write(void) {
    if (this->lazy_write_enabled) {
        flush_dirty_blocks();
        this->lazy_write_enabled = false;
    }
}

void Writer::write_to_cassandra(const TupleRow *keys, const TupleRow *values) {

    if (lazy_write_enabled) {
        //put into dirty_blocks. Skip the repeated 'keys' requests replacing the value.
        tbb::concurrent_hash_map <const TupleRow*, const TupleRow*, Writer::HashCompare>::accessor a;

        if (!dirty_blocks->find(a, keys)) {
            const TupleRow* k = new TupleRow(keys);
            const TupleRow* v = new TupleRow(values);
            if (dirty_blocks->insert(a, k)) {
                a->second = v;
            }
        } else { // Replace value
            delete a->second;
            const TupleRow* v = new TupleRow(values);
            a->second = v;
        }

        if (dirty_blocks->size() > max_calls) {//if too many dirty_blocks
            flush_dirty_blocks();
        }
    } else {
        queue_async_query(keys, values);
    }
}

void Writer::write_to_cassandra(void *keys, void *values) {
    const TupleRow *k = k_factory->make_tuple(keys);
    const TupleRow *v = v_factory->make_tuple(values);
    this->write_to_cassandra(k, v);
    delete (k);
    delete (v);
}

void Writer::write_to_cassandra(void *keys, void *values , const char *value_name) {
    // When trying to write a single attribute of the cassandra table we MUST
    // DISABLE the dirty cache as the complexity to manage the merging phase
    // hides the benefit of it
    disable_lazy_write();

    TupleRowFactory * v_single_factory = new TupleRowFactory(table_metadata->get_single_value(value_name));
    const TupleRow *k = k_factory->make_tuple(keys);
    const TupleRow *v = v_single_factory->make_tuple(values);
    this->write_to_cassandra(k, v);
    delete (v_single_factory);
    delete (k);
    delete (v);
}


/* bind_cassstatement: Prepare an statement and bind it with the passed keys and values */
CassStatement* Writer::bind_cassstatement(const TupleRow* keys, const TupleRow* values) const {
    CassStatement *statement;
    // Check if it is writing the whole set of values or just a single one
    if (table_metadata->get_values()->size() > values->n_elem()) { // Single value written
        if (values->n_elem() > 1)
            throw ModuleException("async_query_execute: only supports 1 or all attributes write");

        ColumnMeta cm = values->get_metadata_element(0);
        const CassPrepared *prepared_query = prepared_partial_queries.at(cm.info["name"]);
        statement = cass_prepared_bind(prepared_query);
        this->k_factory->bind(statement, keys, 0); //error
        TupleRowFactory * v_single_factory = new TupleRowFactory(table_metadata->get_single_value(cm.info["name"].c_str()));
        v_single_factory->bind(statement, values, this->k_factory->n_elements());
        delete(v_single_factory);

    } else { // Whole row written
        statement = cass_prepared_bind(prepared_query);
        this->k_factory->bind(statement, keys, 0); //error
        this->v_factory->bind(statement, values, this->k_factory->n_elements());
    }

    if (!this->disable_timestamps) {
        cass_statement_set_timestamp(statement, keys->get_timestamp());
    }
    return statement;
}

void Writer::queue_async_query(const TupleRow* keys, const TupleRow* values){
    TupleRow *queued_keys = new TupleRow(keys);
    if (!disable_timestamps) queued_keys->set_timestamp(timestamp_gen->next()); // Set write time

    ncallbacks++;
    WriterThread::get(*myconfig).queue_async_query(this, queued_keys, values);
}

CassSession* Writer::get_session() const {
    return session;
}
