import logging
import os
import re

from cassandra.cluster import Cluster

from cassandra.policies import RetryPolicy, RoundRobinPolicy, TokenAwarePolicy


# Set default log.handler to avoid "No handler found" warnings.

stderrLogger = logging.StreamHandler()
f = '%(filename)s: %(levelname)s: %(funcName)s(): %(lineno)d:\t%(message)s'
stderrLogger.setFormatter(logging.Formatter(f))

log = logging.getLogger('hecuba')
log.addHandler(stderrLogger)

if 'DEBUG' in os.environ and os.environ['DEBUG'].lower() == "true":
    log.setLevel(logging.DEBUG)
elif 'HECUBA_LOG' in os.environ:
    log.setLevel(os.environ['HECUBA_LOG'].upper())
else:
    log.setLevel(logging.ERROR)


class _NRetry(RetryPolicy):
    def __init__(self, time_to_retry=5):
        self.time_to_retry = time_to_retry

    def on_unavailable(self, query, consistency, required_replicas, alive_replicas, retry_num):
        if retry_num > self.time_to_retry:
            return self.RETHROW, None
        else:
            return self.RETHROW, None

    def on_write_timeout(self, query, consistency, write_type, required_responses, received_responses, retry_num):
        if retry_num > self.time_to_retry:
            return self.RETHROW, None
        else:
            return self.RETHROW, None

    def on_read_timeout(self, query, consistency, required_responses, received_responses, data_retrieved, retry_num):
        if retry_num > self.time_to_retry:
            return self.RETHROW, None
        else:
            return self.RETHROW, None


class Config:
    class __Config:
        def __init__(self):
            self.configured = False

    instance = __Config()

    def __getattr__(self, item):
        return getattr(Config.instance, item)

    def __init__(self, mock_cassandra=False):
        Config.reset(mock_cassandra=mock_cassandra)

    @staticmethod
    def reset(mock_cassandra=False):
        singleton = Config.instance
        if singleton.configured and singleton.mock_cassandra == mock_cassandra:
            log.info('setting down')
            return

        singleton.mock_cassandra = mock_cassandra
        log.info('setting up configuration with mock_cassandra = %s', mock_cassandra)

        singleton.configured = True

        if 'CREATE_SCHEMA' in os.environ:
            singleton.id_create_schema = int(os.environ['CREATE_SCHEMA'])
        else:
            singleton.id_create_schema = -1

        if mock_cassandra:
            log.info('configuring mock environment')
        else:
            log.info('configuring production environment')
        try:
            singleton.nodePort = int(os.environ['NODE_PORT'])
            log.info('NODE_PORT: %d', singleton.nodePort)
        except KeyError:
            log.warn('using default NODE_PORT 9042')
            singleton.nodePort = 9042

        try:
            singleton.contact_names = os.environ['CONTACT_NAMES'].split(",")
            log.info('CONTACT_NAMES: %s', str.join(" ", singleton.contact_names))
        except KeyError:
            log.warn('using default contact point localhost')
            singleton.contact_names = ['127.0.0.1']

        if hasattr(singleton, 'session'):
            log.warn('Shutting down pre-existent sessions and cluster')
            try:
                singleton.session.shutdown()
                singleton.cluster.shutdown()
            except Exception:
                log.warn('error shutting down')
        try:
            singleton.replication_factor = int(os.environ['REPLICA_FACTOR'])
            log.info('REPLICA_FACTOR: %d', singleton.replication_factor)
        except KeyError:
            singleton.replication_factor = 1
            log.warn('using default REPLICA_FACTOR: %d', singleton.replication_factor)

        try:
            user_defined_execution_name = os.environ['EXECUTION_NAME']
            if user_defined_execution_name == 'hecuba':
                raise RuntimeError('Error: the application keyspace cannot be \'hecuba\'. '
                                   'This keyspace is reserved for storing metadata.')
            singleton.execution_name = user_defined_execution_name
            log.info('EXECUTION_NAME: %s', singleton.execution_name)
        except KeyError:
            singleton.execution_name = 'my_app'
            log.warn('using default EXECUTION_NAME: %s', singleton.execution_name)

        try:
            singleton.number_of_partitions = int(os.environ['NUMBER_OF_BLOCKS'])
            log.info('NUMBER_OF_BLOCKS: %d', singleton.number_of_partitions)
        except KeyError:
            singleton.number_of_partitions = 32
            log.warn('using default NUMBER_OF_BLOCKS: %d', singleton.number_of_partitions)

        try:
            singleton.min_number_of_tokens = int(os.environ['MIN_NUMBER_OF_TOKENS'])
            log.info('MIN_NUMBER_OF_TOKENS: %d', singleton.min_number_of_tokens)
        except KeyError:
            singleton.min_number_of_tokens = 1024
            log.warn('using default MIN_NUMBER_OF_TOKENS: %d', singleton.min_number_of_tokens)

        try:
            singleton.max_cache_size = int(os.environ['MAX_CACHE_SIZE'])
            log.info('MAX_CACHE_SIZE: %d', singleton.max_cache_size)
        except KeyError:
            singleton.max_cache_size = 0
            log.warn('using default MAX_CACHE_SIZE: %d', singleton.max_cache_size)

        try:
            singleton.replication_strategy = os.environ['REPLICATION_STRATEGY']
            log.info('REPLICATION_STRATEGY: %s', singleton.replication_strategy)
        except KeyError:
            singleton.replication_strategy = "SimpleStrategy"
            log.warn('using default REPLICATION_STRATEGY: %s', singleton.replication_strategy)

        try:
            singleton.replication_strategy_options = os.environ['REPLICATION_STRATEGY_OPTIONS']
            log.info('REPLICATION_STRATEGY_OPTIONS: %s', singleton.replication_strategy_options)
        except KeyError:
            singleton.replication_strategy_options = ""
            log.warn('using default REPLICATION_STRATEGY_OPTIONS: %s', singleton.replication_strategy_options)

        if singleton.replication_strategy is "SimpleStrategy":
            singleton.replication = "{'class' : 'SimpleStrategy', 'replication_factor': %d}" % \
                                    singleton.replication_factor
        else:
            singleton.replication = "{'class' : '%s', %s}" % (
                singleton.replication_strategy, singleton.replication_strategy_options)
        try:
            singleton.hecuba_print_limit = int(os.environ['HECUBA_PRINT_LIMIT'])
            log.info('HECUBA_PRINT_LIMIT: %s', singleton.hecuba_print_limit)
        except KeyError:
            singleton.hecuba_print_limit = 1000
            log.warn('using default HECUBA_PRINT_LIMIT: %s', singleton.hecuba_print_limit)

        try:
            singleton.prefetch_size = int(os.environ['PREFETCH_SIZE'])
            log.info('PREFETCH_SIZE: %s', singleton.prefetch_size)
        except KeyError:
            singleton.prefetch_size = 10000
            log.warn('using default PREFETCH_SIZE: %s', singleton.prefetch_size)

        try:
            singleton.write_buffer_size = int(os.environ['WRITE_BUFFER_SIZE'])
            log.info('WRITE_BUFFER_SIZE: %s', singleton.write_buffer_size)
        except KeyError:
            singleton.write_buffer_size = 1000
            log.warn('using default WRITE_BUFFER_SIZE: %s', singleton.write_buffer_size)

        try:
            singleton.write_callbacks_number = int(os.environ['WRITE_CALLBACKS_NUMBER'])
            log.info('WRITE_CALLBACKS_NUMBER: %s', singleton.write_callbacks_number)
        except KeyError:
            singleton.write_callbacks_number = 16
            log.warn('using default WRITE_CALLBACKS_NUMBER: %s', singleton.write_callbacks_number)

        if mock_cassandra:
            class clusterMock:
                def __init__(self):
                    from cassandra.metadata import Metadata
                    self.metadata = Metadata()
                    self.metadata.rebuild_token_map("Murmur3Partitioner", {})

            class sessionMock:

                def execute(self, *args, **kwargs):
                    log.info('called mock.session')
                    return []

                def prepare(self, *args, **kwargs):
                    return self

                def bind(self, *args, **kwargs):
                    return self

            singleton.cluster = clusterMock()
            singleton.session = sessionMock()
        else:
            log.info('Initializing global session')
            try:
                singleton.cluster = Cluster(contact_points=singleton.contact_names, load_balancing_policy=TokenAwarePolicy(RoundRobinPolicy()), port=singleton.nodePort,
                                            default_retry_policy=_NRetry(5))
                singleton.session = singleton.cluster.connect()
                singleton.session.encoder.mapping[tuple] = singleton.session.encoder.cql_encode_tuple
                from hecuba.hfetch import connectCassandra
                # connecting c++ bindings
                connectCassandra(singleton.contact_names, singleton.nodePort)
                if singleton.id_create_schema == -1:
                    queries = [
                        "CREATE KEYSPACE IF NOT EXISTS hecuba  WITH replication = %s" % singleton.replication,
                        """CREATE TYPE IF NOT EXISTS hecuba.q_meta(
                        mem_filter text, 
                        from_point frozen<list<double>>,
                        to_point frozen<list<double>>,
                        precision float);
                        """,
                        'CREATE TYPE IF NOT EXISTS hecuba.np_meta(dims frozen<list<int>>,type int,block_id int);',
                        """CREATE TABLE IF NOT EXISTS hecuba
                        .istorage (storage_id uuid, 
                        class_name text,name text, 
                        istorage_props map<text,text>, 
                        tokens list<frozen<tuple<bigint,bigint>>>,
                        indexed_on list<text>,
                        qbeast_random text,
                        qbeast_meta frozen<q_meta>,
                        numpy_meta frozen<np_meta>,
                        primary_keys list<frozen<tuple<text,text>>>,
                        columns list<frozen<tuple<text,text>>>,
                        PRIMARY KEY(storage_id));
                        """]
                    for query in queries:
                        try:
                            singleton.session.execute(query)
                        except Exception as e:
                            log.error("Error executing query %s" % query)
                            raise e

            except Exception as e:
                log.error('Exception creating cluster session. Are you in a testing env? %s', e)


global config
config = Config()

from hecuba.parser import Parser
from hecuba.storageobj import StorageObj
from hecuba.hdict import StorageDict
from hecuba.hnumpy import StorageNumpy
from hecuba.hfilter import hfilter

if not filter == hfilter:
    import builtins
    builtins.python_filter = filter
    builtins.filter = hfilter

__all__ = ['StorageObj', 'StorageDict', 'StorageNumpy', 'Parser']
