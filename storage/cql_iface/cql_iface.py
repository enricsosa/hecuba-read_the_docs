from collections import OrderedDict
from typing import List, Tuple, FrozenSet, Generator
from uuid import UUID
import uuid


from storage.cql_iface.tests.mockIStorage import IStorage
from .config import _hecuba2cassandra_typemap
from .cql_comm import CqlCOMM
from ..storage_iface import StorageIface
from .tools import generate_token_ring_ranges
from .queries import istorage_read_entry, istorage_prepared_st
from .tools import config
"""
Mockup on how the Cassandra implementation of the interface could work.
"""


class CQLIface(StorageIface):
    hcache_datamodel = []
    # DataModelID - DataModelDef
    data_models_cache = {}
    # StorageID - DataModelID
    object_to_data_model = {}
    # Object Name - Cache
    hcache_by_name = {}
    # Object's class - Cache
    hcache_by_class = {}
    # StorageID - Cache
    hcache_by_id = {}

    def __init__(self):
        pass

    @staticmethod
    def check_values_from_definition(definition):
        if isinstance(definition, dict):
            for v in definition.values():
                CQLIface.check_values_from_definition(v)
        elif isinstance(definition, (list, set, tuple)):
            for v in definition:
                CQLIface.check_values_from_definition(v)
        else:
            try:
                if isinstance(definition.__origin__, (Tuple, FrozenSet)):
                    try:
                        _hecuba2cassandra_typemap[definition.__origin__]
                    except KeyError:
                        raise TypeError(f"The type {definition} is not supported")
            except AttributeError:
                try:
                    _hecuba2cassandra_typemap[definition]
                except KeyError:
                    raise TypeError(f"The type {definition} is not supported")

    def add_data_model(self, definition: dict) -> int:
        if not isinstance(definition, dict):
            raise TypeError("Expected a dict type as a definition")
        if not all(name in definition for name in ["type", "value_id", "fields"]):
            raise KeyError("Expected keys 'type', 'value_id' and 'fields'")
        if not (isinstance(definition["value_id"], dict) and isinstance(definition["fields"], dict)):
            raise TypeError("Expected keys 'value_id' and 'fields' to be dict")
        if not issubclass(definition["type"], IStorage):
            raise TypeError("Class must inherit IStorage")
        dm = sorted(definition.items())
        datamodel_id = hash(str(dm))
        try:
            self.data_models_cache[datamodel_id]
        except KeyError:
            dict_definition = {k: definition[k] for k in ('value_id', 'fields')}
            CQLIface.check_values_from_definition(dict_definition)
            self.data_models_cache[datamodel_id] = definition
            CqlCOMM.register_data_model(datamodel_id, definition)
        return datamodel_id

    def register_persistent_object(self, datamodel_id: int, pyobject: IStorage) -> UUID:
        if not isinstance(pyobject, IStorage):
            raise RuntimeError("Class does not inherit IStorage")
        elif not pyobject.is_persistent():
            raise ValueError("Class needs to be a persistent object, it needs id and name")
        elif datamodel_id is None:
            raise ValueError("datamodel_id cannot be None")
        try:
            data_model = self.data_models_cache[datamodel_id]
        except KeyError:
            raise KeyError("Before making a pyobject persistent, the data model needs to be registered")
        object_id = pyobject.getID()
        self.object_to_data_model[object_id] = datamodel_id
        object_name = pyobject.get_name()
        obj_class = pyobject.__class__.__name__
        CqlCOMM.register_istorage(object_id, object_name, obj_class, data_model)
        CqlCOMM.create_table(object_name, data_model)
        if data_model not in self.hcache_datamodel or object_name not in self.hcache_by_name or object_id not in self.hcache_by_id:
            self.hcache_datamodel.append(datamodel_id)
            hc = CqlCOMM.create_hcache(object_id, object_name, data_model)
            self.hcache_by_class[obj_class] = hc
            self.hcache_by_name[object_name] = hc
            self.hcache_by_id[object_id] = hc
        return object_id

    @staticmethod
    def fill_empty_keys_with_None(keys_dict, data_model):
        data_model = {k: None for k in data_model.keys()}
        return {**data_model, **keys_dict}

    def put_record(self, object_id: UUID, key_list: dict, value_list: dict) -> None:
        try:
            UUID(str(object_id))
        except ValueError:
            raise ValueError("The object_id is not an UUID")
        try:
            self.hcache_by_id[object_id]
        except KeyError:
            raise KeyError("hcache must be registered before in the function register_persistent_object")
        if not isinstance(key_list, dict) and not isinstance(value_list, dict):
            raise TypeError("key_list and value_list must be OrderedDict")
        data_model = self.data_models_cache[self.object_to_data_model[object_id]]

        for v in value_list:
            try:
                if not isinstance(value_list[v], data_model["fields"][v]) and value_list[v] is not None:
                    raise Exception("The value types don't match the data model specification")
            except TypeError:
                if not isinstance(value_list[v], data_model["fields"][v].__origin__):
                    raise TypeError("The value types don't match the data model specification")

        for k in key_list:
            try:
                if not isinstance(key_list[k], data_model["value_id"][k]):
                    raise Exception("The key types don't match the data model specification")
            except TypeError:
                if not isinstance(key_list[k], data_model["value_id"][k].__origin__):
                    raise TypeError("The key types don't match the data model specification")

        values_dict = CQLIface.fill_empty_keys_with_None(value_list, data_model["fields"])
        try:
            self.hcache_by_id[object_id].put_row(list(key_list.values()), list(values_dict.values()),
                                                 list(value_list.keys()))
        except Exception:
            raise Exception("key_list or value_list have some parameter that does not correspond with the data model")

    def get_record(self, object_id: UUID, key_list: OrderedDict) -> List[object]:
        try:
            UUID(str(object_id))
        except ValueError:
            raise ValueError("The object_id is not an UUID")
        try:
            self.hcache_by_id[object_id]
        except KeyError:
            raise KeyError("hcache must be registered before in the function register_persistent_object")

        if not key_list:
            raise ValueError("key_list and value_list cannot be None")
        try:
            result = self.hcache_by_id[object_id].get_row(list(key_list.values()))
        except Exception:
            result = []
        return result

    def split(self, object_id: UUID, subsets: int) -> Generator[UUID, UUID, None]:
        try:
            UUID(str(object_id))
        except ValueError:
            raise ValueError("The object_id is not an UUID")
        if not isinstance(subsets, int):
            raise TypeError("subsets parameter should be an integer")
        from .tools import tokens_partitions
        res = config.execute(istorage_read_entry, [object_id])
        if res:
            res = res.one()
        else:
            raise ValueError("The istorage that identifies the object_id is not registered in the IStorage")
        tokens = generate_token_ring_ranges() if not res.tokens else res.tokens
        for token_split in tokens_partitions(res.table_name.split('.')[0], res.table_name.split('.')[1], tokens, subsets):
            storage_id = uuid.uuid4()
            try:
                config.execute(istorage_prepared_st, [storage_id, res.table_name, res.obj_name+'_block', res.data_model, token_split])
            except Exception:
                raise Exception("The IStorage parameters could not be inserted into the IStorage table")
            yield storage_id