/*
+===========================================================================+
|                          Copyright (c) 1997-2000                          |
+---------------------------------------------------------------------------+
| File Name:                                                                |
|                                                                           |
|    CComPtr.h                                                              |
|                                                                           |
+---------------------------------------------------------------------------+
| Descriptions:                                                             |
|                                                                           |
|    Class CComPtr is a thin wrapper, which provides safe manipulation on   |
|    any interface pointers. It has several OPERATORs to make it like a     |
|    PURE interface pointer. It will AddRef() and Release() interface       |
|    pointer without your concern.                                          |
|                                                                           |
|    Actaully ATL has already provided such Smart Pointer from version 2.0. |
|    I just borrow its concept and implement it in PURE C++ so that it can  |
|    be compiled under any ANSI C/C++ Compliant compiler.                   |
|                                                                           |
+---------------------------------------------------------------------------+
| Developer(s):                                                             |
|                                                                           |
|    Xu Wen Bin                                                             |
|                                                                           |
+===========================================================================+
|                           C H A N G E    L O G                            |
+---------------------------------------------------------------------------+
|                                                                           |
|    09-16-2001    Created.                                                 |
|    09-17-2001    Modified.                                                |
|    02-21-2006    Modified.                                                |
|                                                                           |
+---------------------------------------------------------------------------+
| Note(s):                                                                  |
|                                                                           |
|    CComPtr does not support IUnknown interface. That is, you CAN'T use    |
|    CComPtr<IUnknown> to wrap an IUnknown interface pointer. Under most    |
|    conditions, IUnknown pointer will be provided by OS or HIGH layer code.|
|    So this is a reasonable LIMITATION.                                    |
|                                                                           |
+---------------------------------------------------------------------------+
*/

#ifndef _COMPTR_H_
#define _COMPTR_H_

#define ASSERT assert

template<class INTERFACE, const IID* piid = nullptr>
class CComPtr
{
public:
    CComPtr()
    {
        m_Ptr = nullptr;
    }
    CComPtr(INTERFACE* lPtr)
    {
        m_Ptr = nullptr;

        if (lPtr != nullptr)
        {
            m_Ptr = lPtr;
            m_Ptr->AddRef();
        }
    }
    CComPtr(const CComPtr<INTERFACE, piid>& RefComPtr)
    {
        m_Ptr = nullptr;
        m_Ptr = (INTERFACE*)RefComPtr;

        if (m_Ptr)
        {
            m_Ptr->AddRef();
        }
    }
    CComPtr(IUnknown* pIUnknown, IID iid)
    {
        m_Ptr = nullptr;

        if (pIUnknown != nullptr)
        {
            pIUnknown->QueryInterface(iid, (void**)&m_Ptr);
        }
    }
    ~CComPtr()
    {
        if (m_Ptr)
        {
            m_Ptr->Release();
            m_Ptr = nullptr;
        }
    }

public:
    operator INTERFACE*() const
    {
        //ASSERT(m_Ptr != nullptr);
        return m_Ptr;
    }
    INTERFACE& operator*() const
    {
        ASSERT(m_Ptr != nullptr);
        return *m_Ptr;
    }
    INTERFACE** operator&()
    {
        //ASSERT(m_Ptr != nullptr);
        return &m_Ptr;
    }
    INTERFACE* operator->() const
    {
        ASSERT(m_Ptr != nullptr);
        return m_Ptr;
    }
    INTERFACE* operator=(INTERFACE* lPtr)
    {
        ASSERT(lPtr != nullptr);
        if (IsEqualObject(lPtr))
        {
            return m_Ptr;
        }
        m_Ptr->Release();
        lPtr->AddRef();
        m_Ptr = lPtr;
        return m_Ptr;
    }
    INTERFACE* operator=(IUnknown* pIUnknown)
    {
        ASSERT(pIUnknown != nullptr);
        ASSERT(piid != nullptr);
        pIUnknown->QueryInterface(*piid, (void**)&m_Ptr);
        ASSERT(m_Ptr != nullptr);
        return m_Ptr;
    }
    INTERFACE* operator=(const CComPtr<INTERFACE, piid>& RefComPtr)
    {
        ASSERT(&RefComPtr != nullptr);
        m_Ptr = (INTERFACE*)RefComPtr;

        if (m_Ptr)
        {
            m_Ptr->AddRef();
        }
        return m_Ptr;
    }
    void Attach(INTERFACE* lPtr)
    {
        if (lPtr)
        {
            m_Ptr->Release();
            m_Ptr = lPtr;
        }
    }
    INTERFACE* Detach()
    {
        INTERFACE* lPtr = m_Ptr;
        m_Ptr = nullptr;
        return lPtr;
    }
    void Release()
    {
        if (m_Ptr)
        {
            m_Ptr->Release();
            m_Ptr = nullptr;
        }
    }
    BOOL IsEqualObject(IUnknown* pOther)
    {
        ASSERT(pOther != nullptr);
        IUnknown* pUnknown = nullptr;
        m_Ptr->QueryInterface(IID_IUnknown, (void**)&pUnknown);
        BOOL Result = (pOther == pUnknown) ? TRUE : FALSE;
        pUnknown->Release();
        return Result;
    }

private:
    INTERFACE* m_Ptr;
};

#endif // _COMPTR_H_
