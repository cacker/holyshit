#include "toolbar.h"
#include <vector>
#include <map>
#include <boost/tuple/tuple.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/format.hpp>
#include "hook.h"
#include <boost/foreach.hpp>
#include "command.h"
#include <Shlwapi.h>
#include "../common/command_OD.h"
#include "../sdk/sdk.h"
#include "../common/func.h"

//#pragma pack(1) // fuck!因为OD SDK强制使用了pack，必须加句，否则其他使用如CConfig_Single去地方，跟plugin110.cpp里的不一样，
// 会出现你不能发现的问题，如string莫名其妙崩溃，release编译会有warning C4742警告，千万不要忽略！
// 解决办法，getInstance的实现放到cpp里面去

#define CToolbar_Global (CToolbar::getInstance())

struct TOOLBAR_ITEM
{
    // handle, cmd
    typedef boost::tuple<HBITMAP, std::string> HADLE_CMD;
    std::vector<HADLE_CMD> data;

    // 计算出来的
    size_t iStatus;
    int x;
    int xBegin;

    TOOLBAR_ITEM() : iStatus(0), xBegin(0){}
};

class CToolbar
{
public:
    static CToolbar& getInstance();
    size_t init(const std::string& ini_path);
    void attach(HWND hWnd);

    std::vector<boost::tuple<LONG, LONG>> rect_calc(const LPRECT);
protected:
    TOOLBAR_ITEM get(size_t index);
    std::vector<TOOLBAR_ITEM>::iterator at(int x, int y);
    void draw(HWND hwnd,
        UINT uMsg,
        WPARAM wParam,
        LPARAM lParam,
        bool lbtn_up = false);
    void destory();
    CToolbar();
    ~CToolbar();

    // 窗口相关的代码其实可以仿jmpstack用ATL编写，因为这个类先写成就算了
    static LRESULT CALLBACK WindowProc( HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam );
    void draw_internal(HDC hDC, int x, HBITMAP handle, HPEN hpen1, HPEN hpen2);
    void OnLeftButtonUp(LPARAM lParam);
private:
    std::vector<TOOLBAR_ITEM> m_bmp;
    static WNDPROC m_prevProc;
    HWND m_hWnd;
    HPEN m_pen;
    HPEN m_penWhite;
    HPEN m_penBlack;
};


#define GET_X_LPARAM(lp)                        ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp)                        ((int)(short)HIWORD(lp))

#ifdef HOLYSHIT_EXPORTS
PVOID OrgFillRect = (PVOID)0x00432EC7; // od1
#else
PVOID OrgFillRect = (PVOID)HARDCODE(0x0040C50E); // od2
#endif

size_t CToolbar::init(const std::string& ini_path)
{
    using boost::property_tree::ptree;
    ptree pt;

    size_t size = 0;

    try
    {
        read_ini(ini_path, pt);
        size = pt.get<size_t>("setting.countall", 0);
        int xBegin = pt.get<int>("setting.xbegin");
        int xBegin_cur = xBegin;
        if (size)
        {
            for (size_t i = 0; i<size; ++i)
            {
                try{
                    TOOLBAR_ITEM bd;
                    bd.x = xBegin_cur + 20;

                    size_t count = 1;
                    try
                    {
                        std::string item = boost::str(boost::format("setting.%dcount") % i);
                        count = pt.get<size_t>(item);
                    }catch(...){}

                    try
                    {
                        std::string item = boost::str(boost::format("setting.%dxbegin") % i);
                        bd.xBegin = pt.get<size_t>(item);
                        bd.x += bd.xBegin;
                    }catch(...){}
                    
                    for (size_t j=1; j<= count; ++j)
                    {
                        try
                        {
                            std::string item = boost::str(boost::format("setting.%dimage%d") % i %j);
                            std::string image = pt.get<std::string>(item);
                            item = boost::str(boost::format("setting.%dcommand%d") % i % j);
                            std::string cmd = pt.get<std::string>(item);

                            std::string::size_type pos = ini_path.find_last_of('\\');
                            std::string img_path;
                            img_path = ini_path.substr(0, pos) + "\\" + image;
                            HBITMAP handle = ((HBITMAP)LoadImageA(NULL, img_path.c_str(),IMAGE_BITMAP,0,0,
                                LR_LOADFROMFILE | LR_CREATEDIBSECTION | LR_DEFAULTSIZE));

                            if (handle && !cmd.empty())
                            {
                                bd.data.push_back(boost::make_tuple(handle, cmd));
                            }
                        }catch(...){}
                    }

                    if (bd.data.size())
                    {
                        xBegin_cur = bd.x;
                        m_bmp.push_back(bd);
                    }
                }
                catch(...){}
            }
        }
    }
    catch (...)
    {
    }
    return m_bmp.size();
}

TOOLBAR_ITEM CToolbar::get( size_t index )
{
    return m_bmp.at(index);
}

CToolbar::~CToolbar()
{
    destory();
}
CToolbar::CToolbar()
: m_pen(0)
{

}
void CToolbar::destory()
{
    if (m_prevProc)
    {
        SetWindowLongPtr(m_hWnd, GWLP_WNDPROC, (LONG_PTR)m_prevProc);
    }
    if (m_pen)
    {
        DeleteObject(m_pen);
    }
    std::vector<TOOLBAR_ITEM>::iterator ci = m_bmp.begin();
    for (; ci != m_bmp.end(); ++ci)
    {
        std::vector<TOOLBAR_ITEM::HADLE_CMD>::const_iterator ti = ci->data.begin();
        for (; ti!= ci->data.end(); ++ti)
        {
            TOOLBAR_ITEM::HADLE_CMD temp = *ti;
            HBITMAP handle = boost::get<0>(temp);
            DeleteObject(handle);
        }
        ci->data.clear();
    }
    m_bmp.clear();
}
void CToolbar::OnLeftButtonUp(LPARAM lParam)
{
    int xPos = GET_X_LPARAM(lParam); 
    int yPos = GET_Y_LPARAM(lParam); 
    std::vector<TOOLBAR_ITEM>::iterator ci = at(xPos, yPos);
    if (ci != m_bmp.end())
    {
        std::string exestr = boost::get<1>(ci->data[ci->iStatus]);

        if (0 == exestr.compare(0, 5, "call:"))
        {
            CCommand_Single.Invoke(exestr.substr(5), ci->iStatus);
        }
        else
            ShellExecuteA(NULL, 0, boost::get<1>(ci->data[ci->iStatus]).c_str(), NULL, NULL, SW_NORMAL);
        //MessageBox(0, boost::get<1>(ci->data[ci->iStatus]).c_str(), 0,0);
        

        ++ci->iStatus;
        if (ci->iStatus >= ci->data.size())
        {
            ci->iStatus = 0;
        }
    }
}
LRESULT CALLBACK CToolbar::WindowProc( HWND hwnd,
                            UINT uMsg,
                            WPARAM wParam,
                            LPARAM lParam
                            )
{
    if (uMsg == WM_PAINT)
    {
        CToolbar_Global.draw(hwnd, uMsg, wParam, lParam);
    }
    else if (uMsg == WM_LBUTTONDOWN)
    {
        //SetCapture(hwnd); // 否则移出窗口位置就没有WM_LBUTTONUP消息了。但是OD的又不正常了
        CToolbar_Global.draw(hwnd, uMsg, wParam, lParam,true);
    }
    else if(uMsg == WM_LBUTTONUP)
    {
        //::ReleaseCapture();
        CToolbar_Global.OnLeftButtonUp(lParam);
        SendMessageA(hwnd, WM_PAINT, 0, 0);
    }
    //else if (uMsg == WM_CAPTURECHANGED)
    //{
    //}
    return CallWindowProc(m_prevProc, hwnd, uMsg, wParam, lParam);
}

std::vector<boost::tuple<LONG, LONG>> CToolbar::rect_calc(const LPRECT rc)
{
    std::vector<boost::tuple<LONG, LONG>> ret;

    size_t s = m_bmp.size();
    if (s > 0)
    {
        LONG start = rc->left;
        LONG end = m_bmp[0].x;
        if (m_bmp[0].xBegin)
        {
            end += m_bmp[0].xBegin;
        }

        ret.push_back(boost::make_tuple(start, end)); // 头

        for (size_t i = 1; i < m_bmp.size(); ++i)
        {
            if (m_bmp[i].xBegin)
            {
                start = m_bmp[i].x - m_bmp[i].xBegin - 1; // 不减1会有个小黑点
                end = m_bmp[i].x;
                ret.push_back(boost::make_tuple(start, end));
            }
        }

        ret.push_back(boost::make_tuple(m_bmp[s-1].x + 18, rc->right)); // 尾
    }
    return ret;
}


// 注意__cdecl
int __cdecl Fake_FillRect(HDC hDC,
             LPRECT lprc, 
             HBRUSH hbr )
{
    std::vector<boost::tuple<LONG, LONG>> rc = CToolbar_Global.rect_calc(lprc);
    typedef boost::tuple<LONG, LONG> tuple_type; // 跟map类似需要单独提出来声明
    BOOST_FOREACH(tuple_type a, rc)
    {
        RECT rect = *lprc;
        rect.left = boost::get<0>(a);
        rect.right = boost::get<1>(a);
        FillRect(hDC, &rect, hbr);
    }
    // 最终还会调用原FillRect，但我们这里使它多参数无效
    lprc->left=0;
    lprc->right=0;
    return 0;
}


void __declspec(naked) MyFillRect()
{
    // 这里有一点技巧
    __asm
    {
        call Fake_FillRect;
        jmp OrgFillRect;
    }
}

void hookFillRect()
{
    hook(&(PVOID&)OrgFillRect, MyFillRect);
}
void CToolbar::attach(HWND hWnd)
{
    m_hWnd = hWnd;
#ifdef HOLYSHIT_EXPORTS
    m_pen = CreatePen(0, 3, 0xC0C0C0);
#else
    m_pen = CreatePen(0, 3, 0xF0F0F0);
#endif
    m_penWhite = (HPEN)GetStockObject(WHITE_PEN);
    m_penBlack = (HPEN)GetStockObject(BLACK_PEN);
    m_prevProc = (WNDPROC)SetWindowLongPtr(hWnd,  GWLP_WNDPROC, (LONG_PTR)WindowProc);

    hookFillRect();
    //00432EC4 // 需要hook，屏蔽FillRect
    // 0044FC4C //点击数据窗口显示
    // 00408800 //断点相关
}

void CToolbar::draw_internal(HDC hDC, int x, HBITMAP handle, HPEN hpen1, HPEN hpen2)
{
    HGDIOBJ hOldPen = SelectObject(hDC, m_pen);
    POINT pt;
    MoveToEx(hDC, x + 18, 2, &pt);
    LineTo(hDC, x + 18, 18);

    SelectObject(hDC, hpen1);

    MoveToEx(hDC, x, 18, &pt);
    LineTo(hDC, x, 2);
    LineTo(hDC, x + 18, 2);
    SelectObject(hDC, hpen2);
    MoveToEx(hDC, x, 19, &pt);
    LineTo(hDC, x + 17, 19);
    LineTo(hDC, x + 17, 1);

    HDC hDCComp = CreateCompatibleDC(hDC);
    SelectObject(hDCComp, handle);
    BitBlt(hDC, x + 1, 3, 16, 16, hDCComp, 1, 1, SRCCOPY);
    DeleteObject(hDCComp);

    SelectObject(hDC, hOldPen);
}
void CToolbar::draw(HWND hwnd,
                    UINT uMsg,
                    WPARAM wParam,
                    LPARAM lParam,
                    bool lbtn_up)
{
    if (!lbtn_up) // 全部画
    {
        HDC hDC = GetDC(hwnd);
        std::vector<TOOLBAR_ITEM>::const_iterator ci = m_bmp.begin();
        for (; ci != m_bmp.end(); ++ci)
        {
            HBITMAP handle = boost::get<0>(ci->data[ci->iStatus]);
            draw_internal(hDC, ci->x, handle, m_penWhite, m_penBlack);
        }
        ReleaseDC(hwnd, hDC);
    }
    else
    {
        int xPos = GET_X_LPARAM(lParam); 
        int yPos = GET_Y_LPARAM(lParam); 
        std::vector<TOOLBAR_ITEM>::iterator ci = at(xPos, yPos);
        if (ci != m_bmp.end())
        {
            HDC hDC = GetDC(hwnd);
            HBITMAP handle = boost::get<0>(ci->data[ci->iStatus]);
            draw_internal(hDC, ci->x, handle, m_penBlack, m_penWhite);
            ReleaseDC(hwnd, hDC);
        }
    }
    
}

// x:0-17 y:2-18
std::vector<TOOLBAR_ITEM>::iterator CToolbar::at( int x, int y )
{
    std::vector<TOOLBAR_ITEM>::iterator ci = m_bmp.begin();
    if (y > 2 && y <18)
    {
        for (; ci != m_bmp.end(); ++ci)
        {
            if (x > ci->x
                && x < (ci->x + 17)
                )
            {
                return ci;
            }
        }
    }
    return m_bmp.end();
}

CToolbar& CToolbar::getInstance()
{
    static CToolbar a;
    return a;
}


WNDPROC CToolbar::m_prevProc = NULL;


int Toolbar::_ODBG_Plugininit( int ollydbgversion,HWND hw, ulong *features )
{
#ifdef HOLYSHIT_EXPORTS
    // toolbar相关初始化
    std::tstring szTB = m_IConfigForToolbar->get_ini_path();
    if (PathFileExistsA(szTB.c_str()))
    {
        if(CToolbar_Global.init(szTB.c_str()))//"D:\\src\\vc\\holyshit\\common\\test.ini"
        {
            CToolbar_Global.attach((HWND)Plugingetvalue(VAL_HWMAIN));
        }
    }
#endif
    return 0;
}

int Toolbar::ODBG2_Plugininit( void )
{
#ifndef HOLYSHIT_EXPORTS

    // toolbar相关初始化
    std::tstring szTB = m_IConfigForToolbar->get_ini_path();
    if (PathFileExistsW(szTB.c_str()))
    {
        std::string path = wstring2string(szTB.c_str(), CP_ACP);
        if(CToolbar_Global.init(path))//"D:\\src\\vc\\holyshit\\common\\test.ini"
        {
            CToolbar_Global.attach(hwollymain);
        }
    }
#endif
    return 0;
}

Toolbar::Toolbar( IConfigForToolbar* i)
: m_IConfigForToolbar(i)
{

}
