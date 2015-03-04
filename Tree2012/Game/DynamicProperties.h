#pragma once

#include <string>
#include <map>

using namespace std;

class Prop
{
public:
    virtual ~Prop()
    {
    }
};

template<typename T>
class Property : public Prop
{
private:
    T data;
public:
    virtual ~Property()
    {
    }

    Property(T d)
    {
        data = d;
    }

    T GetValue()
    {
        return data;
    }
};

class Properties
{
private:
    map<string, Prop*> props;
public:
    ~Properties()
    {
        map<string, Prop*>::iterator iter;

        for(iter = props.begin(); iter != props.end(); ++iter)
            delete (*iter).second;

        props.clear();
    }

    template<typename T>
    void Add(string name, T data)
    {
        props[name] = new Property<T>(data);
    }

    template<typename T>
    T Get(string name)
    {
        Property<T>* p = (Property<T>*)props[name];

        return p->GetValue();
    }
};
