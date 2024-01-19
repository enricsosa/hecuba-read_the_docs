.. _applications:

The basics
==========

In this chapter we describe in detail the interface provided by the Hecuba. We also illustrate how the supported data types and operations can be applied.

Supported Data Types and Collections
************************************

Immutable types supported:
--------------------------

Data types
^^^^^^^^^^^

* ``str``, ``bool``, ``decimal``, ``float``, ``int``, ``blob``, ``tuple``, ``buffer``.

* ``double`` floating point numbers will be stored as double precision numbers.

Collections
^^^^^^^^^^^

* ``numpy.ndarray``.
* ``frozenset`` supported in StorageObj only.

Mutable collections supported:
--------------------------

* ``dict``.

* ``set`` Subject to restrictions, supported only by StorageDict (development underway).

* ``list`` to group a set of values for a given key in a StorageDict. E.g. ``dict[0] = [1,2,3]``.

Hecuba Data Classes
*******************

Storage Object
--------------------------

The *StorageObj* is the simplest abstraction provided by Hecuba. It acts like a ``namedtuple``, or a ``dataclass``, where the user can define attributes and access them. However, in this case, the user can choose which attributes will be persisted to the data store.

To declare instances of the StorageObj, the user first needs to define a class inheriting from the *StorageObj* as well as define the data model of the persistent attributes. The format of the data model is a Python comment with one line per attribute. Each line must start with the keyword *@Classfield* and continue with the name of the attributes and its data type specification.

.. code-block:: python

    class ClassName(StorageObject):
        '''
        @ClassField attribute_name attribute_type
        '''

For example, the following code shows the definition of a class containing an attribute of type integer.

.. code-block:: python

    class MyClass(StorageObj):
        '''
        @ClassField MyAttribute_1 int
        '''

When the user needs to use collections as attributes, the syntax needs to be further elaborated. For example, to define a Python dictionary it is necessary to specify the type of the keys and the type of the values. In this case, after the attribute type we can find the rest of the specifications within angular brackets.

.. code-block:: python

    class ClassName(StorageObj):
        '''
        @ClassField attribute_name attribute_type <attribute_type_specification>
        '''

For example, the following code adds a dictionary attribute: the key is of type ``Integer`` and the value a ``str``.

.. code-block:: python

    class MyClass(StorageObj):
        '''
        @ClassField MyAttribute_1 int
        @ClassField MyAttribute_2 dict <<int>, str>
        '''

Each additional level required to complete a specification type can be added within angle brackets. For example, the following code adds the specification of a dictionary that has a key of type tuple, which is composed of an ``Integer`` and a ``str``, and that has a value of type ``Integer``.

.. code-block:: python

    class MyClass(StorageObj):
        '''
        @ClassField MyAttribute_1 int
        @ClassField MyAttribute_2 dict <<int>, str>
        @ClassField MyAttribute_3 dict <<int, str>, int>
        '''

Attributes of type ``dict`` allow the programmer to assign a name to each component of the dictionary (keys and values). These names can help users to give semantic meaning to the data, for instance when accessing the results of a dictionary or when exploring the persistent data with external tools.

.. code-block:: python

    class MyClass(StorageObj):
        '''
        @ClassField MyAttribute_1 int
        @ClassField MyAttribute_2 dict <<int>, str>
        @ClassField MyAttribute_3 dict <<int, str>, int>
        @ClassField MyAttribute_4 dict <<mykey1:int, mykey2:str>, myvalue:int>
        '''
