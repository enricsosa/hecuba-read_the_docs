#ifndef ISTORAGE_H
#define ISTORAGE_H


#include "configmap.h"
#include "HecubaSession.h"
#include "debug.h"
#include "ObjSpec.h"

//class HecubaSession; //Forward declaration

#define KEYS    1
#define COLUMNS 0

class IStorage {
    /* This class represents an instantiated object (StorageDict) and can be
     * used as a gateway to modify the associated table. */

    public:
        IStorage();
        IStorage(std::string id_model, std::string id_object, uint64_t* storage_id, CacheTable* reader);
        ~IStorage();


        IStorage(const IStorage& src);
        IStorage &operator = (const IStorage& src);

        virtual void setItem(void* key, IStorage* value) {};
        virtual void setItem(void* keys, void* values) {};

        void send(void* key, IStorage* value);
        void send(void* key, void* value);
        virtual void send_values(const void* value);

        virtual void setAttr(const std::string attr_name, IStorage* value) {};
        virtual void setAttr(const std::string attr_name, void* value) {};


        void setClassName(std::string name);
        void setIdModel(std::string name);
        const std::string& getClassName();
        const std::string& getIdModel(); // FQName of the class
        std::pair<std::string, std::string> getKeyspaceAndTablename( const std::string& FQIDmodel ) const;
        HecubaSession & getCurrentSession() const;
        const std::string& getTableName() const;
        void setTableName(std::string tableName);
        void make_persistent(const std::string id_obj);
        bool is_pending_to_persist();
        void set_pending_to_persist();

        virtual void getAttr(const std::string& attr_name, void * valuetoreturn) {};
        virtual ObjSpec generateObjSpec() {};
        virtual void getItem(const void* key, void * valuetoreturn) {};

        virtual Writer * getDataWriter()const { return dataWriter;}

        uint64_t* getStorageID();
        const std::string& getName() const;

        std::shared_ptr<CacheTable>getDataAccess()const ;
        void setCache(const CacheTable &cache);

        void sync(void);

        void enableStream();
        void configureStream(std::string topic);
        bool isStream();


        void writePythonSpec();
        virtual ObjSpec& getObjSpec() ;
        void setObjSpec(const ObjSpec &oSpec);
        void setPythonSpec(std::string pSpec);
        std::string getPythonSpec();
        void setObjectName(std::string id_obj);
        std::string getObjectName();
        std::string getTableName();
        // the definition of at least one virtual function is necessary to use rtti capabilities
        // and be able to infer subclass name from a method in the base clase
        virtual void generatePythonSpec() {};
        virtual void assignTableName(const std::string& id_object, const std::string& id_model) {};
        virtual void persist_metadata(uint64_t * c_uuid) {};
        virtual void persist_data() {};
        virtual void setPersistence (uint64_t *uuid) {};
        virtual std::vector<std::pair<std::string, std::string>> getValuesDesc() { };
        virtual std::vector<std::pair<std::string, std::string>> getPartitionKeys() {};
        virtual std::vector<std::pair<std::string, std::string>> getClusteringKeys() {};
        virtual void initialize_dataAcces() {};

        void getByAlias(const std::string& name) ;

        void extractMultiValuesFromQueryResult(void *query_result, void *valuetoreturn, int type) ;
    private:

        ObjSpec IStorageSpec;
        std::string pythonSpec = "";
        std::string tableName; // name of the table without keyspace
        bool pending_to_persist = false;
        bool persistent = false;

        enum valid_writes {
            SETATTR_TYPE,
            SETITEM_TYPE,
        };
        std::string generate_numpy_table_name(std::string attributename);


        config_map keysnames;
        config_map keystypes;
        config_map colsnames;
        config_map colstypes;

        uint64_t* storageid=nullptr;

        std::string id_obj=""; // Name to identify this 'object' [keyspace.name]
        std::string id_model=""; // Type name to be found in model "class_name" (FQName)
        std::string class_name=""; // plain class name

        bool streamEnabled=false;

        Writer* dataWriter = nullptr; /* Writer for entries in the object. EXTRACTED from 'dataAccess' */
        std::shared_ptr<CacheTable> dataAccess = nullptr; /* Cache of written/read elements */

    protected:
        bool delayedObjSpec = false;
        std::string PythonDisclaimerString = "#### THIS IS A FILE AUTO-GENERATED BY THE HECUBA C++ INTERFACE    ####\n####     WITH THE EQUIVALENT PYTHON DEFINITION OF THE CLASS       ####\n#### It will be overwritten each time that a program that uses    ####\n#### the C++ interface of Hecuba and uses an object of this class ####\n####                 is executed in this folder                  ####\n\n\n";
        struct metadata_info {
            std::string name;
            std::string class_name;
            ArrayMetadata numpy_metas;
        };
        const struct metadata_info  getMetaData(uint64_t* uuid) const;
        void init_persistent_attributes(const std::string& id_object, uint64_t *uuid);
        void * deep_copy_attribute_buffer(bool isKey, const void* src, uint64_t src_size, uint32_t num_attrs) ;
        void extractFromQueryResult(std::string value_type, uint32_t value_size, void *query_result, void *valuetoreturn) const;
        /* convert_IStorage_to_UUID: Given a value (basic or persistent) convert it to the same value or its *storage_id* if it is a persistent one. Returns True if type is converted (aka was an IStorage). */
        bool convert_IStorage_to_UUID(char * dst, const std::string& value_type, const void* src, int64_t src_size) const ;
        void initializeClassName(std::string class_name);
};
#endif /* ISTORAGE_H */
