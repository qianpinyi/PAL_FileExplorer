/*
	PAL_FileExplorer Ver-1.4
	20.9.22
	By:qianpinyi
	//Refactoring version 1.4 20.12.22
	Refactoring version 1.5 21.6.25~ ; (╯°□°）╯︵ ┻━
		...
		21.6.27: Icon;
		21.6.29: TopAreaButton;
		21.6.30: Search;
		21.7.1: MainViewType;Sensize;
		21.7.2: ShortcutKey;
		21.7.14: SearchTextFileContent;
		21.7.31: Recompile with PAL_GUI's update--Touch control;
		21.8.1:  Apply PUI's newest feature; Gesture control.
		21.8.12: Sensize with history and compare; Virtual File and Dir;
		21.8.14: Transparent widgets;
		21.8.18: SortFunction;
		21.10.6: ExeIconExtract;
		21.11.13: ShellContextMenu; TopArea LargeLayer;
		21.11.14: Get ExeIcon in Thread;
		21.12.11: Update to V1.6. Completed FileOperationPart(almost completed,over 1000lines,cost over a week); ChangeMonitor;
		21.12.12: Merge BVT and LVT related code to MainView; Multi select; PopupFunc_SearchInThisDir;
		21.12.14: Open PowerShell and WSL Bash in CurrentPath;
		22.1.16: Adjust the color; MainInfo show item counts; RecentHighLight;
		22.1.23: Fix thumbnail bug of some of wstring path by BasicIO;
		22.1.25: Show path in WindowTitle; EnableRememberPathHistory(if not,Clear RecentList when quit; Remember HistoryPath and CurrentPath);
		22.2.1: Complete SettingPage; AboutPage; BugFix;
		22.2.6: TabView related;
		22.2.7: TabView complete;
		22.2.22~24: IShellItemImageFactory->GetImage(); PAL_ThreadWorker(Include PAL_Thread,PAL_Mutex,PAL_Semaphore); Better icon get method(PAL_ThreadWorker);
		22.3.13: Update to V1.7. Completed IconGet related(cost about 2 week);
		22.3.14: Fixed some bugs and improved some functions;Start check restore history path;Start to specific path;Synchronize with system clipboard;
		22.3.18: Fixed some bugs; RecycleBin and some related operation;
		22.3.22: Replace SLRE with cpp std::regex; Bug fixing;
		22.3.27: Operation_XXX and related reconstruct(Menu,OperationButton);
		22.3.29: Bug fix; EditAddressBar;
		22.3.30: Small imporves: SettingLevel change; Restore from recycle bin; Refresh after complete FileOperationTask; 
		22.4.1: Update to V1.8; Upload to MS Store; Some improves;
*/
#define PAL_GUI_UseWindowsIME 1
#define PAL_SDL_IncludeSyswmHeader 1
#include <PAL_BasicFunctions/PAL_Charset.cpp>
#include <PAL_BasicFunctions/PAL_System.cpp>
#include <PAL_BasicFunctions/PAL_Config.cpp>
#include <PAL_BasicFunctions/PAL_File.cpp>
#include <PAL_BasicFunctions/PAL_Time.cpp>
#include <PAL_GUI/PAL_GUI_0.cpp>
#include PAL_SDL_imageHeaderPath
#include <PAL_Images/PAL_Thumbnails.cpp>
#include <PAL_DataStructure/PAL_LeastRecentlyUsed.cpp>
#include <PAL_Platform/PAL_Windows.cpp>
#include <PAL_Platform/PAL_Windows_ShellContextMenu.cpp>
#include <PAL_Thread/PAL_ThreadWorker.cpp>
#include <regex>
using namespace PAL_GUI;
using namespace Charset;
using namespace PAL_Parallel;

const string ProgramName="PAL_FileExplorer",
			 ProgramVersion="1.8",
			 ProgramNameVersion=ProgramName+" "+ProgramVersion,
			 ProgramVersionDate="2022.4.1";
			 
enum
{
	PFE_PathType_None=0,
	PFE_PathType_File,
	PFE_PathType_Dir,
	PFE_PathType_Volume,
	PFE_PathType_MainPage,
	PFE_PathType_Library,
	PFE_PathType_Network,
	PFE_PathType_Cloud,
	PFE_PathType_Setting,
	PFE_PathType_About,
	PFE_PathType_Search,
//	PFE_PathType_PhotoViewer,
	PFE_PathType_VirtualFile,
	PFE_PathType_VirtualDir,
	PFE_PathType_RecycleBin
};

enum
{
	PFE_Path_VolumeCode_Normal=0,
	PFE_Path_VolumeCode_Remote
};

enum
{
	PFE_Path_RecycleBinCode_Normal=0,
	PFE_Path_RecycleBinCode_Local,//Same as Normal
	PFE_Path_RecycleBinCode_All,
	PFE_Path_RecycleBinCode_Volume
};

struct PFE_Path
{
	int type=0,code=0;
	string str,name;
	
	bool operator ! () const
	{return type==0;}
	
	bool operator != (const PFE_Path &tar) const
	{return type!=tar.type||code!=tar.code||str!=tar.str;}
	
	bool operator == (const PFE_Path &tar) const
	{return type==tar.type&&code==tar.code&&str==tar.str;}
	
	bool SameType(const PFE_Path &tar) const
	{return type==tar.type&&code==tar.code;}
	
	bool operator < (const PFE_Path &tar) const
	{
		if (type==tar.type)
			if (str==tar.str)
//				if (name==tar.name)//??
					return code<tar.code;
//				else return name<tar.name;
			else return str<tar.str;
		else return type<tar.type;
	}
	
	unsigned int SizeForIO() const
	{return 4+4+4+str.length()+4+name.length();}
	
	PFE_Path(int _type,const string &_str,const string &_name,int _code=0):type(_type),code(_code),str(_str),name(_name) {}
	
	PFE_Path() {}
};
PFE_Path PFE_Path_MainPage=PFE_Path(PFE_PathType_MainPage,"",PUIT("此电脑")),
		 PFE_Path_Setting=PFE_Path(PFE_PathType_Setting,"",PUIT("设置")),
		 PFE_Path_RecycleBin=PFE_Path(PFE_PathType_RecycleBin,"",PUIT("回收站"));

inline BasicBinIO& operator << (BasicBinIO &bio,const PFE_Path &path)
{return bio<<path.type<<path.code<<path.str<<path.name;}

inline BasicBinIO& operator >> (BasicBinIO &bio,PFE_Path &path)
{return bio>>path.type>>path.code>>path.str>>path.name;}

string PFE_Path_Encode(const PFE_Path &path)
{
	MemoryIO mio(path.SizeForIO());
	if (mio.LastError())
		DD[2]<<"PFE_Path_Encode:mio.LastError "<<mio.LastError()<<endl;
	BasicBinIO bio(&mio,0);
	bio<<path;
	if (bio.LastError())
		DD[2]<<"PFE_Path_Encode:bio.LastError "<<bio.LastError()<<" bio.IOError "<<bio.IOError()<<endl;
	return BinToStr(mio.Memory(),mio.MemorySize());
}

PFE_Path PFE_Path_Decode(const string &str)
{
	int size=str.length()/2;
	MemoryIO mio(size);
	if (mio.LastError())
		DD[2]<<"PFE_Path_Decode:mio.LastError "<<mio.LastError()<<endl;
	StrToBin(str,mio.Memory());
	BasicBinIO bio(&mio,0);
	PFE_Path path;
	bio>>path;
	if (bio.LastError())
		DD[2]<<"PFE_Path_Decode:bio.LastError "<<bio.LastError()<<" bio.IOError "<<bio.IOError()<<endl;
	return path;
}

struct MainViewData
{
	PFE_Path path;
	Widgets *fa=NULL;
	TinyText *TT_Name=NULL,
			 *TT_First=NULL,//optional;first attribute,depending on item type and user's setting.
			 *TT_Second=NULL;//optional
	PictureBox <Triplet <PFE_Path,int,int> > *PB_Pic=NULL;
	ProgressBar *ProBar_Space=NULL;//optional
	CheckBox <PFE_Path> *CB_Choose=NULL;
	BorderRectLayer *BRL_Border=NULL;
//	int RecentLevel=0;
//	unsigned long long Size=0;
	
	bool operator == (const MainViewData &tar) const
	{return path==tar.path;}
	
	MainViewData(const PFE_Path &_path):path(_path) {}
	
	MainViewData() {}
}MainViewData_None;

struct LVT_QuickListData
{
	PFE_Path path;
	int ListType=0,NumCode=0;
	TinyText *TT_Name=NULL,
			 *TT_FullPath=NULL,
			 *TT_Size=NULL;
	
	bool operator == (const LVT_QuickListData &tar) const
	{return path==tar.path;}
	
	LVT_QuickListData(const PFE_Path &_path,int _listtype):path(_path),ListType(_listtype) {}
	
	LVT_QuickListData() {}
};

PictureBoxI *PB_Background=NULL;
//Layer *Lay_TopArea=NULL,
Layer *Lay_AddrSecArea=nullptr,
	  *Lay_SettingPage=nullptr,
	  *Lay_TopTabBar=nullptr;
AddressSection <PFE_Path> *AddrSec_TopAddress=NULL;
ButtonI *Bu_GoBack=NULL,
		*Bu_GoForward=NULL,
		*Bu_GoUp=NULL,
		*Bu_Sensize=NULL,
		*Bu_ChangeView1=NULL,
		*Bu_ChangeView2=NULL,
		*Bu_ReversedSort=nullptr;
TwinLayerWithDivideLine *TwinLay_DivideTreeBlock=NULL;
LargeLayerWithScrollBar *Larlay_Centre=NULL,
						*Larlay_LeftView=NULL,
						*Larlay_TopArea=NULL;
BlockViewTemplate <MainViewData> *BVT_MainView=NULL;
ListViewTemplate <MainViewData> *LVT_MainView=NULL;
ListViewTemplate <LVT_QuickListData> *LVT_QuickList=NULL,
									 *LVT_RecentList=NULL;
TextEditLineI *TEL_SearchFile=NULL;
ProgressBar *ProBar_Top=NULL,
			*ProBar_CurrentVolumeSize=NULL;
TinyText *TT_InfoOfDir=NULL,
		 *TT_MainInfo=NULL,
		 *TT_SensizeState=NULL,
		 *TT_CurrentVolumeSize=NULL;
PhotoViewer *PV_InnerPhotoViewer=NULL;
GestureSenseLayerI *GSL_GlobalGesture=NULL;
DropDownButtonI *DDB_SortMethod=NULL;

enum
{
	CurrentMainView_None=0,
	CurrentMainView_BVT_0,
	CurrentMainView_BVT_1,
	CurrentMainView_BVT_2,
	CurrentMainView_BVT_3,
	CurrentMainView_BVT_4,
	CurrentMainView_BVT_5,
	CurrentMainView_BVT_6,
	CurrentMainView_BVT_7,
	CurrentMainView_BVT_8,
	CurrentMainView_BVT_9,
	CurrentMainView_BVT_10,
	CurrentMainView_LVT_Normal,
	CurrentMainView_LVT_SearchResult,
	CurrentMainView_Setting,
	CurrentMainView_BVT_RecycleBin
};
atomic_int CurrentMainView(CurrentMainView_BVT_0);
PAL_Config PFE_Cfg;
PFE_Path CurrentPath;
string ProgramPath,ProgramDataPath,UserDataPath;
vector <PFE_Path> FileNodeClipboard;
int CoMoClipboardType=0;//0:copy 1:move
bool SynchronizeWithSystemClipboard=0;
set <string> AcceptedPictureFormat;
map <string,SharedTexturePtr> ItemIconPic;
map <string,string> SharedIconMap;
const char InvalidFileName[10]="/\\:*?\"<>|";
int RecentListLRULimit=10;//if 0,means disable
LRU_LinkHashTable <PFE_Path,int> LRU_RecentList(10);
unsigned int CurrentThemeColorCode=0,
			 WidgetsOpacityPercent=60;
bool HaveBackgroundPic=0,
	 EnableBackgroundCoverColor=0;
RGBA BackgroundCoverColor=RGBA_BLACK*0.5;
int BackgroundPictureBoxMode=21;
int PathSortMode=0,PathSortReversed=0;
bool EnableAutoSelectLastPath=1;
PAL_TimeP TimeForSetCurrentPath;
int ShowFullPathInWindowTitle=1;//0:none 1:name 2:fullpath
bool ShowAfternameInName=1,
	 ShowHiddenFile=1,
	 ShowSettingBackgroundPicOnRightClickMenu=1;
int SwitchToPictureViewPercent=80;
const int TopAreaHeight=100;
int InitBackgroundPictureLoadMode=0;//0:Sync load raw 1:Async load raw 2:Cache only(Uncompleted) 3:Sync cache async raw(Uncompleted)
bool ShowRealDeleteInRigthClickMenu=0;

//DynamicUserColor Area
RGBA DUC_DirBlockColor[3],
	 DUC_FileBlockColor[3],
	 DUC_VolumeBlockColor[3],
	 DUC_VDirBlockColor[3],
	 DUC_VFileBlockColor[3],
	 DUC_QuictListColor[3],
	 DUC_RecentListColor[3],
	 DUC_ButtonColor[3],
	 DUC_InvalidButtonColor[3],
	 DUC_ClearThemeColor[8],
	 DUC_SensizeTextColor,
	 DUC_MessageLayerBackground,
	 DUC_MessageLayerTopAreaColor,
	 DUC_Menu1Background,
	 DUC_ClearBackground0,
	 DUC_TopAreaBackground,
	 DUC_LeftAreaBackground,
	 DUC_MainAreaBackground,
	 DUC_BackgroundCover,
	 DUC_SettingItemBackground;

void SetUIWidgetsOpacity(int per)
{
	#define TCCUC(DUC,CO) ThemeColor.ChangeUserColor(DUC,CO)
	per=EnsureInRange(per,0,100);
	if (per==0||!HaveBackgroundPic)
	{
		TCCUC(DUC_DirBlockColor[0],RGBA(255,240,203,255));
		TCCUC(DUC_DirBlockColor[1],RGBA(255,223,182,255));
		TCCUC(DUC_DirBlockColor[2],RGBA(255,197,146,255));
		TCCUC(DUC_FileBlockColor[0],ThemeColor[0]<<RGBA(255,255,255,130));
		TCCUC(DUC_FileBlockColor[1],ThemeColor[1]);
		TCCUC(DUC_FileBlockColor[2],ThemeColor[3]);
		TCCUC(DUC_VolumeBlockColor[0],ThemeColor[2]);
		TCCUC(DUC_VolumeBlockColor[1],ThemeColor[4]);
		TCCUC(DUC_VolumeBlockColor[2],ThemeColor[6]);
		TCCUC(DUC_VDirBlockColor[0],RGBA(255,245,231,255));
		TCCUC(DUC_VDirBlockColor[1],RGBA(255,240,203,255));
		TCCUC(DUC_VDirBlockColor[2],RGBA(255,222,190,255));
		TCCUC(DUC_VFileBlockColor[0],RGBA(240,240,240,255));
		TCCUC(DUC_VFileBlockColor[1],RGBA(210,210,210,255));
		TCCUC(DUC_VFileBlockColor[2],RGBA(180,180,180,255));
		for (int i=0;i<=2;++i)
			TCCUC(DUC_RecentListColor[i],ThemeColor[i*2]),
			TCCUC(DUC_QuictListColor[i],ThemeColor[i*2]),
			TCCUC(DUC_ButtonColor[i],ThemeColor[i*2+1]),
			TCCUC(DUC_InvalidButtonColor[i],ThemeColor.BackgroundColor[i*2+1]);
		for (int i=0;i<=7;++i)
			TCCUC(DUC_ClearThemeColor[i],ThemeColor[i]);
		TCCUC(DUC_SensizeTextColor,RGBA(91,112,255,255));
		TCCUC(DUC_MessageLayerBackground,RGBA_WHITE);
		TCCUC(DUC_MessageLayerTopAreaColor,ThemeColor[1]);
		TCCUC(DUC_Menu1Background,RGBA(250,250,250,255));
		TCCUC(DUC_ClearBackground0,ThemeColor.BackgroundColor[0]);
		TCCUC(DUC_TopAreaBackground,RGBA_TRANSPARENT);
		TCCUC(DUC_LeftAreaBackground,RGBA_TRANSPARENT);
		TCCUC(DUC_MainAreaBackground,RGBA_TRANSPARENT);
		TCCUC(DUC_BackgroundCover,RGBA_TRANSPARENT);
		TCCUC(DUC_SettingItemBackground,ThemeColor[0]<<RGBA(255,255,255,130));
	}
	else
	{
		double percent=per/100.0;
		int colorAlpha=EnsureInRange(percent*255,1,255);
		TCCUC(DUC_DirBlockColor[0],RGBA(255,240,203,colorAlpha));
		TCCUC(DUC_DirBlockColor[1],RGBA(255,223,182,colorAlpha));
		TCCUC(DUC_DirBlockColor[2],RGBA(255,197,146,colorAlpha));
		TCCUC(DUC_FileBlockColor[0],(ThemeColor[0]<<RGBA(255,255,255,130)).AnotherA(colorAlpha));
		TCCUC(DUC_FileBlockColor[1],ThemeColor[1].AnotherA(colorAlpha));
		TCCUC(DUC_FileBlockColor[2],ThemeColor[3].AnotherA(colorAlpha));
		TCCUC(DUC_VolumeBlockColor[0],ThemeColor[2].AnotherA(colorAlpha));
		TCCUC(DUC_VolumeBlockColor[1],ThemeColor[4].AnotherA(colorAlpha));
		TCCUC(DUC_VolumeBlockColor[2],ThemeColor[6].AnotherA(colorAlpha));
		TCCUC(DUC_VDirBlockColor[0],RGBA(255,245,231,colorAlpha));
		TCCUC(DUC_VDirBlockColor[1],RGBA(255,240,203,colorAlpha));
		TCCUC(DUC_VDirBlockColor[2],RGBA(255,222,190,colorAlpha));
		TCCUC(DUC_VFileBlockColor[0],RGBA(240,240,240,colorAlpha));
		TCCUC(DUC_VFileBlockColor[1],RGBA(210,210,210,colorAlpha));
		TCCUC(DUC_VFileBlockColor[2],RGBA(180,180,180,colorAlpha));
		for (int i=0;i<=2;++i)
			TCCUC(DUC_RecentListColor[i],ThemeColor[i*2].AnotherA(colorAlpha)),
			TCCUC(DUC_QuictListColor[i],ThemeColor[i*2].AnotherA(colorAlpha)),
			TCCUC(DUC_ButtonColor[i],ThemeColor[i*2+2].AnotherA(colorAlpha)),
			TCCUC(DUC_InvalidButtonColor[i],ThemeColor.BackgroundColor[i*2+1].AnotherA(colorAlpha));
		for (int i=0;i<=7;++i)
			TCCUC(DUC_ClearThemeColor[i],ThemeColor[i].AnotherA(colorAlpha));
		TCCUC(DUC_SensizeTextColor,RGBA(91,112,255,255));
		TCCUC(DUC_MessageLayerBackground,RGBA(255,255,255,colorAlpha));
		TCCUC(DUC_MessageLayerTopAreaColor,ThemeColor[1].AnotherA(colorAlpha));
		TCCUC(DUC_Menu1Background,RGBA(250,250,250,colorAlpha));
		TCCUC(DUC_ClearBackground0,ThemeColor.BackgroundColor[0].AnotherA(colorAlpha));
//		TCCUC(DUC_TopAreaBackground,RGBA(255,255,255,colorAlpha-60));
//		TCCUC(DUC_LeftAreaBackground,RGBA(255,255,255,colorAlpha-40));
//		TCCUC(DUC_MainAreaBackground,RGBA(255,255,255,colorAlpha-100));
		TCCUC(DUC_BackgroundCover,EnableBackgroundCoverColor?BackgroundCoverColor:RGBA_TRANSPARENT);
		TCCUC(DUC_SettingItemBackground,(ThemeColor[0]<<RGBA(255,255,255,130)).AnotherA(colorAlpha));
		WidgetsOpacityPercent=per;
		PFE_Cfg("WidgetsOpacityPercent")=llTOstr(WidgetsOpacityPercent);
	}
	PUI_Window::SetNeedFreshScreenAll();
	#undef TCCUC
}

void RegisterDynamicUserColor()
{
	DD[4]<<"RegisterDynamicUserColor"<<endl;
	#define RegDUC(DUC) (DUC=ThemeColor.SetUserColor(RGBA_NONE))
	for (int i=0;i<=2;++i)
	{
		RegDUC(DUC_DirBlockColor[i]);
		RegDUC(DUC_FileBlockColor[i]);
		RegDUC(DUC_VolumeBlockColor[i]);
		RegDUC(DUC_VDirBlockColor[i]);
		RegDUC(DUC_VFileBlockColor[i]);
		RegDUC(DUC_RecentListColor[i]);
		RegDUC(DUC_QuictListColor[i]);
		RegDUC(DUC_ButtonColor[i]);
		RegDUC(DUC_InvalidButtonColor[i]);
	}
	for (int i=0;i<=7;++i)
		RegDUC(DUC_ClearThemeColor[i]);
	RegDUC(DUC_SensizeTextColor);
	RegDUC(DUC_MessageLayerBackground);
	RegDUC(DUC_MessageLayerTopAreaColor);
	RegDUC(DUC_Menu1Background);
	RegDUC(DUC_ClearBackground0);
	RegDUC(DUC_TopAreaBackground);
	RegDUC(DUC_LeftAreaBackground);
	RegDUC(DUC_MainAreaBackground);
	RegDUC(DUC_BackgroundCover);
	RegDUC(DUC_SettingItemBackground);
	#undef RegDUC
	DD[5]<<"RegisterDynamicUserColor"<<endl;
}
//End of DynamicUserColor

//CommonFunctionAndPredefine Area
double MainView_GetScrollBarPercent();

PFE_Path PFE_Path_GetFa(const PFE_Path &path,bool NoSystem=0);

enum
{
	GetAllFileInDir_To_User=0,
	GetAllFileInDir_To_BVT,
	GetAllFileInDir_To_LVT,
	GetAllFileInDir_To_AddrSec,
	GetAllFileInDir_To_RecycleBin
};

long long GetAllFileInDir(const PFE_Path &path,vector <PFE_Path> &vec,int to);

void RefreshCurrentPath();

void SetNeedUpdateOperationButton();

void SetMainViewContent(int p,const PFE_Path &path);

bool SetBackgroundPic(const string &path,int from);

enum
{
	SetCurrentPathFrom_User=0,
	SetCurrentPathFrom_GoBack,
	SetCurrentPathFrom_GoForward,
	SetCurrentPathFrom_GoUp,
	SetCurrentPathFrom_Refresh,
//	SetCurrentPathFrom_BVT,
//	SetCurrentPathFrom_LVT,
	SetCurrentPathFrom_MainView,
	SetCurrentPathFrom_AddrSec,
	SetCurrentPathFrom_AddrSecList,
	SetCurrentPathFrom_LeftList,
	SetCurrentPathFrom_Search,
	SetCurrentPathFrom_TabView
};
void SetCurrentPath(const PFE_Path &tar,int from);

void SetCurrentMainView(int,bool);

void SetLeftListItem(int p,const PFE_Path &path,int targetList);//targetList 0:QuickList 1:RecentList

inline CurrentMainViewIsNormal(int mainview)
{return NotInSet(mainview,CurrentMainView_None,CurrentMainView_Setting);}

void WarningMessageButton(unsigned int beep,const string &title,const string &msg,const string &button=PUIT("确定"))
{
	SetSystemBeep(beep);
	auto msgbx=new MessageBoxButtonI(0,title,msg);
	msgbx->SetBackgroundColor(DUC_MessageLayerBackground);
	msgbx->SetTopAreaColor(DUC_MessageLayerTopAreaColor);
	msgbx->AddButton(button,nullptr,0);
}

template <class T> void SetButtonDUCColor(Button <T> *bu)
{
	for (int i=0;i<=2;++i)
		bu->SetButtonColor(i,DUC_ButtonColor[i]);
}

template <class T> int TextEditLineColorChange(TextEditLine <T> *tel,bool resetNormal)
{
	if (resetNormal)
		for (int i=0;i<=2;++i)
			tel->SetBorderColor(i,RGBA_NONE);
	else
	{
		tel->SetBorderColor(0,{255,198,139,255});
		tel->SetBorderColor(1,{255,156,89,255});
		tel->SetBorderColor(2,{255,89,0,255});
	}
	return resetNormal;
}

inline bool IsFileExsit(const wstring &path,int filter=-1)//filter:-1:no filter 0:file 1:dir
{
	int x=GetFileAttributesW(path.c_str());
	if (x==-1) return 0;
	else if (filter==-1||(filter!=0)==bool(x&FILE_ATTRIBUTE_DIRECTORY))
		return 1;
	else return 0;
}

inline bool IsFileExsit(const string &path,int filter=-1)
{return IsFileExsit(DeleteEndBlank(Utf8ToUnicode(path)),filter);}

inline bool IsDirEmpty(const wstring &path)
{
	bool flag=1;
	long long hFiles=0;
	_wfinddatai64_t da;
	if ((hFiles=_wfindfirsti64((path+L"\\*").c_str(),&da))!=-1)
		do
			if (wcscmp(da.name,L".")!=0&&wcscmp(da.name,L"..")!=0)
				flag=0;
		while (flag&&_wfindnexti64(hFiles,&da)==0);
	_findclose(hFiles);
	return flag;
}

inline bool IsDirEmpty(const string &path)
{return IsDirEmpty(DeleteEndBlank(Utf8ToUnicode(path)));}

inline bool IsVolume(const string &path)
{return path.length()==2&&InRange(path[0],'A','Z')&&path[1]==':';}//?

inline string GetBaseVolume(const string &path)
{return path.length()<2||!InRange(path[0],'A','Z')||NotInSet(path[1],':')?"":path.substr(0,2);}//??

inline bool IsDir(const wstring &path)
{
	int x=GetFileAttributesW(path.c_str());
	if (x==-1) return 0;
	else if (x&FILE_ATTRIBUTE_DIRECTORY)
		return 1;
	else return 0;
}

inline bool IsDir(const string &path)
{return IsDir(DeleteEndBlank(Utf8ToUnicode(path)));}

inline int CreateDirectoryR(const wstring &path)
{
	if (path==L"") return 1;
	if (!IsFileExsit(path,1))//??
	{
		CreateDirectoryR(GetPreviousBeforeBackSlash(path));
		if (!CreateDirectoryW(path.c_str(),NULL))
			return 2;
	}
	return 0;
}

inline int CreateDirectoryR(const string &path)
{return CreateDirectoryR(DeleteEndBlank(Utf8ToUnicode(path)));}

inline PFE_Path PathStrToPFEPath(const string &path)//??
{
	if (IsVolume(path)) return PFE_Path(PFE_PathType_Volume,path,GetLastAfterBackSlash(path));
	else if (IsDir(path)) return PFE_Path(PFE_PathType_Dir,path,GetLastAfterBackSlash(path));
	else return PFE_Path(PFE_PathType_File,path,GetLastAfterBackSlash(path));
}

void SystemOpenSpecificFile(const string &path)
{
	DD[0]<<"OpenSpecificFile "<<path<<endl;
	string fa=GetPreviousBeforeBackSlash(path);
	ShellExecuteW(0,L"open",(Utf8ToUnicode(path)).c_str(),L"",(Utf8ToUnicode(fa+"\\")).c_str(),SW_SHOWNORMAL);
}

int SyncInnerClipboardToSystemClipboard()
{
	if (!SynchronizeWithSystemClipboard)
		return 0;
	vector <string> vec;
	for (const auto &vp:FileNodeClipboard)
		if (InThisSet(vp.type,PFE_PathType_Dir,PFE_PathType_File))
			vec.push_back(vp.str);
	if (PAL_Platform::SetClipboardFiles(MainWindow->GetWindowsHWND(),vec,CoMoClipboardType?1:0))
		return 1;
	return 0;
}

int SyncSystemClipboardToInnerClipboard()
{
	if (!SynchronizeWithSystemClipboard)
		return 0;
	vector <string> vec;
	int flag=PAL_Platform::GetClipboardFiles(MainWindow->GetWindowsHWND(),vec);
	if (flag==-1&&vec.empty()) return 2;//??
	if (flag!=-1)
		CoMoClipboardType=flag?1:0;
	FileNodeClipboard.clear();
	for (const auto &vp:vec)
		if (vp!="")
			FileNodeClipboard.push_back(PathStrToPFEPath(vp));
	return 0;
}
//End of CommonFunctionAndPredefine

//TabView Area
Widgets::WidgetType UserWidget_TabDataLayer=Widgets::GetNewWidgetTypeID("TabDataLayer"),
					UserWidget_TabManager=Widgets::GetNewWidgetTypeID("TabManager");
template <class T> class TabManager;
template <class T> class TabDataLayer:public BaseButton
{
	friend class TabManager <T>;
	protected:
		TabManager <T> *tabMa=nullptr;
		Button <TabDataLayer*> *bu=nullptr;
		SharedTexturePtr Pic;
		int PicWidth=30,
			CloseBuWidth=30;
		string Title;
		int Width=0,
			Index=0;
		bool Selected=0;
		RGBA ButtonColor[3]{ThemeColorM[0],ThemeColorM[2],ThemeColorM[4]},
			 SelectColor[3]{ThemeColorM[1],ThemeColorM[3],ThemeColorM[5]},
			 TextColor=ThemeColorMT[0],
			 SelectTextColor=ThemeColorM[7];

		void InitSubWidgets();
		
		virtual void TriggerButtonFunction(bool isSubButton);

		inline int GetPicWidth() const
		{
			if (!Pic) return 0;
			else return PicWidth;
		}
		
		inline int GetCloseButtonWidth() const
		{
			if (!bu) return 0;
			else return CloseBuWidth;
		}
		
		virtual void Show(Posize &lmt)
		{
			Win->RenderFillRect(lmt,ThemeColor(Selected?SelectColor[stat]:ButtonColor[stat]));
			Win->RenderCopyWithLmt(Pic(),Posize(0,0,GetPicWidth(),rPS.h).Shrink(3),lmt);
			Win->RenderDrawText(Title,Posize(gPS.x+GetPicWidth()+5,gPS.y,Width-GetPicWidth()-GetCloseButtonWidth()-10,gPS.h),lmt,-1,ThemeColor(Selected?SelectTextColor:TextColor));
			Win->Debug_DisplayBorder(gPS);
		}
		
		virtual void CalcPsEx()
		{
			BaseButton::CalcPsEx();
			if (bu)
				bu->SetrPS(Posize(rPS.w-rPS.h,0,rPS.h,rPS.h));
		}
		
	public:
		T data;
		
		inline int GetIndex() const
		{return Index;}
		
		inline const string& GetTitle() const
		{return Title;}
		
		inline void UpdateWidth();
		
		inline void SetTitle(const string &str)
		{
			Title=str;
			UpdateWidth();
		}
		
		inline void SetIcon(const SharedTexturePtr &pic)
		{
			Pic=pic;
			UpdateWidth();
		}
		
		inline TabManager<T>* GetTabManager() const
		{return tabMa;}
		
		virtual ~TabDataLayer();
		
		TabDataLayer(const WidgetID &_id,TabManager <T> *_fa,const Posize &_rPS,const T &_data,const string &title)
		:BaseButton(_id,UserWidget_TabDataLayer,_fa,_rPS),tabMa(_fa),data(_data)
		{
			ThisButtonSolveSubClick=1;
			ThisButtonSolveExtraClick=1;
			InitSubWidgets();
			SetTitle(title);
		}
};

template <class T> class TabManager:public Widgets
{
	friend class TabDataLayer <T>;
	protected:
		LargeLayerWithScrollBar *larlay=nullptr;
		vector <TabDataLayer<T>*> TabLayers;
		int TabDataCnt=0,
			SumWidth=0,//not include gap and side
			CurrentTab=-1;
		int TabGap=3,
			AddButtonWidth=30;
		Range TabTextRange={100,300};
		bool ReverseTab=0;
		Button <TabManager*> *Bu_Add=nullptr;
		void (*func)(TabDataLayer<T>*,int)=nullptr;//int:0:Create 1:SwitchTo 2:RightClick 3:Delete 4:Clear
		void (*addFunc)(TabManager<T>*)=nullptr;

		inline int GetAddButtonWidth()
		{return AddButtonWidth;}
		
		void UpdatePosize()
		{
			for (int i=0,s=0;i<=TabDataCnt;++i)
			{
				Posize ps(0,0,i==TabDataCnt?GetAddButtonWidth():TabLayers[i]->Width,rPS.h);
				if (ReverseTab)
					ps.x=rPS.w-ps.w-s;
				else ps.x=s;
				s+=ps.w+TabGap;
				if (i<TabDataCnt)
					TabLayers[i]->SetrPS(ps);
				else if (Bu_Add!=nullptr)
					Bu_Add->SetrPS(ps);
			}
		}
		
		void UpdateTotalWidth()
		{
			rPS.w=SumWidth+TabDataCnt*TabGap+GetAddButtonWidth();
			larlay->SetMinLargeAreaSize(rPS.w,0);
		}
		
		virtual void CalcPsEx()
		{
			Posize faPs=larlay->LargeArea()->GetrPS();
			rPS.h=faPs.h;
			rPS.x=ReverseTab&&rPS.w<=faPs.w?faPs.w-rPS.w:0;
			UpdatePosize();
			Widgets::CalcPsEx();
		}
		
	public:
		void SwitchTab(int tar,bool triggerFunc=1)
		{
			if (TabDataCnt==0||!InRange(tar,-1,TabDataCnt-1)) return;
			if (CurrentTab!=-1)
				TabLayers[CurrentTab]->Selected=0;
			CurrentTab=tar;
			if (tar!=-1)
			{
				TabLayers[tar]->Selected=1;
				if (triggerFunc&&func!=nullptr)
					func(TabLayers[tar],1);
			}
		}
		
		void AddTab(int pos,const T &data,const string &title,bool switchToThis=1,bool triggerFunc=1)
		{
			pos=EnsureInRange(pos,0,TabDataCnt);
			TabDataLayer <T> *tdl=new TabDataLayer<T>(0,this,Posize(0,0,TabTextRange.l,rPS.h),data,title);
			TabLayers.insert(TabLayers.begin()+pos,tdl);
			if (pos<=CurrentTab)
				++CurrentTab;
			++TabDataCnt;
			for (int i=pos;i<TabDataCnt;++i)
				TabLayers[i]->Index=i;
			if (triggerFunc&&func!=nullptr)
				func(tdl,0);
			if (switchToThis)
				SwitchTab(pos,triggerFunc);//tdl->Width is known after here.
			Win->SetNeedUpdatePosize();
		}
		
		void CloseTab(int pos,bool triggerFunc=1)
		{
			if (TabDataCnt==0) return;
			pos=EnsureInRange(pos,0,TabDataCnt-1);
			TabDataLayer <T> *tdl=TabLayers[pos];
			if (triggerFunc&&func!=nullptr)
				func(tdl,3);
			if (pos==CurrentTab)
				if (CurrentTab==TabDataCnt-1)
					SwitchTab(CurrentTab-1,triggerFunc);
				else SwitchTab(CurrentTab+1,triggerFunc),--CurrentTab;
			else if (pos<CurrentTab)
				--CurrentTab;
			tdl->DelayDelete();
			TabLayers.erase(TabLayers.begin()+pos);
			--TabDataCnt;
			for (int i=pos;i<TabDataCnt;++i)
				TabLayers[i]->Index=i;
			Win->SetNeedUpdatePosize();
		}
		
		void ResetTargetArea(Widgets *fa,PosizeEX *psex,bool reverse)
		{
			larlay->SetFa(fa);
			larlay->ReAddPsEx(psex);
			ReverseTab=reverse;
			Win->SetNeedUpdatePosize();
		}
		
		inline LargeLayerWithScrollBar* GetLarLay() const
		{return larlay;}
		
		inline void SetFunc(void (*_func)(TabDataLayer<T>*,int))
		{func=_func;}
		
		inline void SetAddFunc(void (*_func)(TabManager<T>*))
		{addFunc=_func;}
		
		inline TabDataLayer<T>* GetTabDataLayer(int pos)
		{
			if (TabDataCnt==0) return nullptr;
			else return TabLayers[EnsureInRange(pos,0,TabDataCnt-1)];
		}
		
		inline TabDataLayer<T>* CurrentTabDataLayer()
		{
			if (CurrentTab==-1) return nullptr;
			else return TabLayers[CurrentTab];
		}
		
		inline int GetTabCnt() const
		{return TabDataCnt;}
		
		~TabManager()
		{
			for (auto vp:TabLayers)
			{
				if (func!=nullptr)
					func(vp,4);
				delete vp;
			}
		}
		
		TabManager(const WidgetID &_id,Widgets *_fa,PosizeEX *psex,bool reverse):Widgets(_id,UserWidget_TabManager),ReverseTab(reverse)
		{
			larlay=new LargeLayerWithScrollBar(0,_fa,psex);
			larlay->SetScrollBarWidth(0);//???
			SetFa(larlay->LargeArea());
			Bu_Add=new Button <TabManager*> (0,this,ZERO_POSIZE,"+",
				[](TabManager *&This)
				{
					if (This->addFunc!=nullptr)
						This->addFunc(This);
				},this);
		}
};

template <class T> void TabDataLayer<T>::InitSubWidgets()
{
	bu=new Button <TabDataLayer<T>*> (0,this,Posize(0,0,rPS.h,rPS.h)/*??*/,PUIT("×"),
		[](TabDataLayer<T> *&This)
		{
			This->tabMa->CloseTab(This->Index);
		},this);
}

template <class T> void TabDataLayer<T>::UpdateWidth()
{
	int oldWidth=Width;
	Width=tabMa->TabTextRange.EnsureInRange(GetStringWidth(Title)+10)+GetPicWidth()+GetCloseButtonWidth();
	if (Width!=oldWidth)
	{
		tabMa->SumWidth+=Width-oldWidth;
		tabMa->UpdateTotalWidth();
		Win->SetNeedUpdatePosize();
	}
}

template <class T> TabDataLayer<T>::~TabDataLayer()
{
	tabMa->SumWidth-=Width;
	tabMa->UpdateTotalWidth();
	Win->SetNeedUpdatePosize();
}

template <class T> void TabDataLayer<T>::TriggerButtonFunction(bool isSubButton)
{
	if (isSubButton)
		if (tabMa->func!=nullptr)
			tabMa->func(this,2);
		else DoNothing;
	else if (PUI_Event::NowSolvingEvent()->type==PUI_Event::Event_MouseEvent
		&&PUI_Event::NowSolvingEvent()->MouseEvent()->which==PUI_MouseEvent::Mouse_Middle)
		tabMa->CloseTab(Index);
	else if (!Selected)
		tabMa->SwitchTab(Index);
}

struct HistoryPathData
{
	PFE_Path path;
	int MainViewType;
	double MainViewScrollPercentY;
	int SortMode;
	bool SortReversed;
	
	HistoryPathData(const PFE_Path &_path,int _mainviewtype,double _mainviewscrollpercentY,int _sortmode,bool _sortreversed)
	:path(_path),MainViewType(_mainviewtype),MainViewScrollPercentY(_mainviewscrollpercentY),SortMode(_sortmode),SortReversed(_sortreversed) {}
	
	HistoryPathData(const PFE_Path &_path):path(_path),MainViewType(::CurrentMainView),MainViewScrollPercentY(0),SortMode(::PathSortMode),SortReversed(::PathSortReversed) {}
	
	HistoryPathData() {}
};

inline BasicBinIO& operator << (BasicBinIO &bio,const HistoryPathData &hpd)
{return bio<<hpd.path<<hpd.MainViewType<<hpd.MainViewScrollPercentY<<hpd.SortMode<<hpd.SortReversed;}

inline BasicBinIO& operator >> (BasicBinIO &bio,HistoryPathData &hpd)
{return bio>>hpd.path>>hpd.MainViewType>>hpd.MainViewScrollPercentY>>hpd.SortMode>>hpd.SortReversed;}

void UpdateCurrentPathContext();//Call this when change PathContext
struct PathContext
{
	vector <HistoryPathData> PathHistory,PopHistory;
	HistoryPathData CurrentPathWithExtraData;
	
	void GoBack()
	{
		UpdateCurrentPathContext();
		PopHistory.push_back(CurrentPathWithExtraData);
		CurrentPathWithExtraData=PathHistory.back();
		PathHistory.pop_back();
	}

	void GoForward()
	{
		UpdateCurrentPathContext();
		PathHistory.push_back(CurrentPathWithExtraData);
		CurrentPathWithExtraData=PopHistory.back();
		PopHistory.pop_back();
	}
	
	void PushNew()
	{
		UpdateCurrentPathContext();
		PathHistory.push_back(CurrentPathWithExtraData);
		PopHistory.clear();
	}
	
	PathContext(const PFE_Path &firstPath):CurrentPathWithExtraData(firstPath) {}
	PathContext() {}
}*CurrentPathContext=nullptr;//Should always not NULL
vector <PathContext*> AllPathContext;//Synchronize with which in TabManager
int CurrentPathContextPos=0;
TabManager <PathContext*> *TM_Tab=nullptr;
bool EnableShowSingleTab=1;

inline BasicBinIO& operator << (BasicBinIO &bio,const PathContext &pc)//Clear these datas in cfg when struct components changed!!!
{return bio<<pc.PathHistory<<pc.PopHistory<<pc.CurrentPathWithExtraData;}

inline BasicBinIO& operator >> (BasicBinIO &bio,PathContext &pc)
{return bio>>pc.PathHistory>>pc.PopHistory>>pc.CurrentPathWithExtraData;}

void PathContext_Encode(const PathContext &pc,string &str)
{
	MemoryIO mio;
	if (mio.LastError())
		DD[2]<<"PathContext_Encode:mio.LastError "<<mio.LastError()<<endl;
	BasicBinIO bio(&mio,0);
	bio<<pc;
	if (bio.LastError())
		DD[2]<<"PathContext_Encode:bio.LastError "<<bio.LastError()<<" bio.IOError "<<bio.IOError()<<endl;
	str=BinToStr(mio.Memory(),mio.Size());
}

void PathContext_Decode(const string &str,PathContext &pc)
{
	int size=str.length()/2;
	MemoryIO mio(size);
	if (mio.LastError())
		DD[2]<<"PathContext_Decode:mio.LastError "<<mio.LastError()<<endl;
	StrToBin(str,mio.Memory());
	BasicBinIO bio(&mio,0);
	bio>>pc;
	if (bio.LastError())
		DD[2]<<"PathContext_Decode:bio.LastError "<<bio.LastError()<<" bio.IOError "<<bio.IOError()<<endl;
}

inline void UpdateCurrentPathContext()
{
	CurrentPathContext->CurrentPathWithExtraData.path=CurrentPath;
	CurrentPathContext->CurrentPathWithExtraData.MainViewType=CurrentMainView;
	CurrentPathContext->CurrentPathWithExtraData.MainViewScrollPercentY=MainView_GetScrollBarPercent();
	CurrentPathContext->CurrentPathWithExtraData.SortMode=PathSortMode;
	CurrentPathContext->CurrentPathWithExtraData.SortReversed=PathSortReversed;
}

enum
{
	TabMode_None=0,
	TabMode_Top,
	TabMode_Addr
};
int CurrentTabMode=TabMode_None;

void TabManagerFunc(TabDataLayer<PathContext*> *tdl,int mode)
{
	switch (mode)
	{
		case 0:
			AllPathContext.insert(AllPathContext.begin()+tdl->GetIndex(),tdl->data);
			if (tdl->GetIndex()<=CurrentPathContextPos)
				++CurrentPathContextPos;
			break;
		case 1:
			UpdateCurrentPathContext();
			CurrentPathContextPos=tdl->GetIndex();
			CurrentPathContext=tdl->data;
			SetCurrentPath(CurrentPathContext->CurrentPathWithExtraData.path,SetCurrentPathFrom_TabView);
			break;
		case 2:
		{
			vector <MenuData<Doublet<WidgetPtr,int> > > menudata;
			auto menufunc=[](Doublet<WidgetPtr,int> &data)
			{
				if (!data.a||CurrentTabMode==TabMode_None) return;
				TabDataLayer<PathContext*> *tdl=(TabDataLayer<PathContext*>*)data.a.Target();
				switch (data.b)
				{
					case 0:	tdl->GetTabManager()->SwitchTab(tdl->GetIndex());																											break;
					case 1:	tdl->GetTabManager()->AddTab(tdl->GetIndex()+1,new PathContext(tdl->data->CurrentPathWithExtraData.path),tdl->data->CurrentPathWithExtraData.path.name);	break;
					case 2:	tdl->GetTabManager()->AddTab(tdl->GetIndex()+1,new PathContext(PFE_Path_MainPage),PFE_Path_MainPage.name);													break;
					case 3:	tdl->GetTabManager()->AddTab(tdl->GetIndex()+(CurrentTabMode==TabMode_Top?0:1),new PathContext(PFE_Path_MainPage),PFE_Path_MainPage.name);					break;
					case 4:	tdl->GetTabManager()->AddTab(tdl->GetIndex()+(CurrentTabMode==TabMode_Top?1:0),new PathContext(PFE_Path_MainPage),PFE_Path_MainPage.name);					break;
					case 5:	tdl->GetTabManager()->CloseTab(tdl->GetIndex());																											break;
				}
			};
			menudata.push_back(MenuData<Doublet<WidgetPtr,int> >(PUIT("切换到标签页"),menufunc,{tdl->This(),0}));
			menudata.push_back(MenuData<Doublet<WidgetPtr,int> >(0));
			menudata.push_back(MenuData<Doublet<WidgetPtr,int> >(PUIT("复制标签页"),menufunc,{tdl->This(),1}));
			menudata.push_back(MenuData<Doublet<WidgetPtr,int> >(0));
			menudata.push_back(MenuData<Doublet<WidgetPtr,int> >(PUIT("新建标签页"),menufunc,{tdl->This(),2}));
			menudata.push_back(MenuData<Doublet<WidgetPtr,int> >(PUIT("在左边新建标签页"),menufunc,{tdl->This(),3}));
			menudata.push_back(MenuData<Doublet<WidgetPtr,int> >(PUIT("在右边新建标签页"),menufunc,{tdl->This(),4}));
			menudata.push_back(MenuData<Doublet<WidgetPtr,int> >(0));
			menudata.push_back(MenuData<Doublet<WidgetPtr,int> >(PUIT("关闭标签页"),menufunc,{tdl->This(),5}));
			new Menu1<Doublet<WidgetPtr,int> >(0,menudata);
			break;
		}
		case 3:
			delete tdl->data;
			AllPathContext.erase(AllPathContext.begin()+tdl->GetIndex());
			if (tdl->GetIndex()<CurrentPathContextPos)
				--CurrentPathContextPos;
			if (AllPathContext.empty())
				PUI_SendEvent(new PUI_Event(PUI_Event::Event_Quit));
			break;
		case 4:	break;
	}
}

void TabManagerAddFunc(TabManager <PathContext*> *tm)
{
	tm->AddTab(1e9,new PathContext(PFE_Path_MainPage),PFE_Path_MainPage.name);
}

void TabModeTop_Prepare(bool onoff)
{
	if (onoff)
	{
		Lay_TopTabBar=new Layer(0,PB_Background,new PosizeEX_Fa6(2,3,0,0,0,30));
		Lay_TopTabBar->SetLayerColor(DUC_ClearThemeColor[0]);
		
		Larlay_TopArea->ReAddPsEx(new PosizeEX_Fa6(2,3,0,0,35,TopAreaHeight));
		Lay_AddrSecArea->ReAddPsEx(new PosizeEX_Fa6(2,3,0,0,35+TopAreaHeight+5,30));
		double per=TwinLay_DivideTreeBlock->GetDivideLinePosition();
		TwinLay_DivideTreeBlock->ReAddPsEx(new PosizeEX_Fa6(2,2,0,0,35+TopAreaHeight+40,20));
		TwinLay_DivideTreeBlock->SetDivideLinePosition(per);
	}
	else
	{
		DelayDeleteToNULL(Lay_TopTabBar);
		
		Larlay_TopArea->ReAddPsEx(new PosizeEX_Fa6(2,3,0,0,0,TopAreaHeight));
		Lay_AddrSecArea->ReAddPsEx(new PosizeEX_Fa6(2,3,0,0,TopAreaHeight+5,30));
		double per=TwinLay_DivideTreeBlock->GetDivideLinePosition();
		TwinLay_DivideTreeBlock->ReAddPsEx(new PosizeEX_Fa6(2,2,0,0,TopAreaHeight+40,20));
		TwinLay_DivideTreeBlock->SetDivideLinePosition(per);
	}
}

bool EnableTextEditLineForAddressArea=1;
const Widgets::WidgetType UserWidget_LayerForAddrSecArea=Widgets::GetNewWidgetTypeID("LayerForAddrSecArea");
class LayerForAddrSecArea:public BaseButton
{
	protected:
		AddressSection <PFE_Path> *AddrSec=nullptr;
		TabManager <PathContext*> *TabMa=nullptr;
		WidgetPtr Tel;
		
		virtual void TriggerButtonFunction(bool isSubButton)
		{
			if (isSubButton)
				;//...
			else
				if (EnableTextEditLineForAddressArea)
				{
					if (Tel.Valid())
						Tel->DelayDelete();
					auto TEL=new TextEditLine<int>(0,this,new PosizeEX_Fa6_Full);
					Tel=TEL->This();
					TEL->SetText(CurrentPath.str,0);
					TEL->SetEmptyText(PUIT("请输入要访问的路径:"));
					TEL->SetEnterFunc([](int&,const stringUTF8 &str,auto *tel,bool isenter)
					{
						if (isenter)
						{
							string s=str.cppString();
							if (IsFileExsit(s,1))
							{
								SetCurrentPath(PathStrToPFEPath(s),SetCurrentPathFrom_User);
								tel->StopTextInput();
							}
							else WarningMessageButton(SetSystemBeep_Error,PUIT("错误"),PUIT("目标路径非法！"));
						}
					},0);
					TEL->SetExtraFunc([](int&,int ee,auto *tel)
					{
						if (ee==TextEditLine<int>::EE_EndInput)
							tel->DelayDelete();
					});
					TEL->StartTextInput();
				}
		}
		
		virtual void _UpdateWidgetsPosize()
		{
			if (nxtBrother!=NULL)
				UpdateWidgetsPosizeOf(nxtBrother);
			if (!Enabled) return;
			CalcPsEx();
			if (TabMa!=nullptr)
			{
				TemporarySetEnabled(AddrSec,0);//Is this OK??
				if (childWg!=NULL)
					UpdateWidgetsPosizeOf(childWg);
				TemporarySetEnabled(AddrSec,1);
				if (TabMa->GetrPS().w<rPS.w>>1)
					AddrSec->SetrPS({0,0,rPS.w-TabMa->GetrPS().w-10,rPS.h});
				else AddrSec->SetrPS({0,0,(rPS.w>>1)-10,rPS.h});
				AddrSec->ForceUpdateWidgetTreePosize();
			}
			else if (childWg!=NULL)
				UpdateWidgetsPosizeOf(childWg);
		}
		
	public:
		void SetAddrSec(AddressSection <PFE_Path> *addrsec)
		{AddrSec=addrsec;}
		
		void SetTabMa(TabManager <PathContext*> *tabma)
		{
			if (TabMa==tabma) return;
			if (tabma==nullptr)
				AddrSec->ReAddPsEx(new PosizeEX_Fa6_Full);
			else AddrSec->RemoveAllPsEx();
			TabMa=tabma;
		}
		
		LayerForAddrSecArea(const WidgetID &_ID,Widgets *_fa,PosizeEX *psex)
		:BaseButton(_ID,UserWidget_LayerForAddrSecArea,_fa,psex) {}
}*Lay_AddrSecAreaCenter=nullptr;

void SwitchTabMode(int mode,bool clearOtherTab=1)
{
	if (CurrentTabMode==mode) return;
	DD[4]<<"SwitchTabMode "<<mode<<endl;
	if (CurrentTabMode==TabMode_None)
	{
		if (mode==TabMode_Top)
		{
			TabModeTop_Prepare(1);
			TM_Tab=new TabManager<PathContext*>(0,Lay_TopTabBar,new PosizeEX_Fa6(2,3,5,5,0,30),0);
		}
		else
		{
			TM_Tab=new TabManager<PathContext*>(0,Lay_AddrSecAreaCenter,new PosizeEX_Fa6(2,2,-0.5,0,0,0),1);
			Lay_AddrSecAreaCenter->SetTabMa(TM_Tab);
		}
		TM_Tab->SetFunc(TabManagerFunc);
		TM_Tab->SetAddFunc(TabManagerAddFunc);
		for (int i=0;i<AllPathContext.size();++i)
			TM_Tab->AddTab(1e9,AllPathContext[i],AllPathContext[i]->CurrentPathWithExtraData.path.name,i==CurrentPathContextPos,0);
	}
	else if (mode==TabMode_None)
	{
		TM_Tab->SetFunc(nullptr);
		TM_Tab->GetLarLay()->DelayDelete();//!
		TM_Tab=nullptr;
		if (CurrentTabMode==TabMode_Addr)
			Lay_AddrSecAreaCenter->SetTabMa(nullptr);
		else TabModeTop_Prepare(0);
		for (auto vp:AllPathContext)
			if (vp!=CurrentPathContext)
				delete vp;
		AllPathContext.clear();
		AllPathContext.push_back(CurrentPathContext);
		CurrentPathContextPos=0;
	}
	else if (mode==TabMode_Top)
	{
		Lay_AddrSecAreaCenter->SetTabMa(nullptr);
		TabModeTop_Prepare(1);
		TM_Tab->ResetTargetArea(Lay_TopTabBar,new PosizeEX_Fa6(2,3,5,5,0,30),0);
	}
	else
	{
		TM_Tab->ResetTargetArea(Lay_AddrSecAreaCenter,new PosizeEX_Fa6(2,2,-0.5,0,0,0),1);
		TabModeTop_Prepare(0);
		Lay_AddrSecAreaCenter->SetTabMa(TM_Tab);
	}
	CurrentTabMode=mode;
	PFE_Cfg("CurrentTabMode")=llTOstr(mode);
	DD[5]<<"SwitchTabMode"<<endl;
}

void SavePathHistoryData(bool checkConfigFlag=1)
{
	DD[4]<<"SavePathHistoryData"<<endl;
	if (!checkConfigFlag||InThisSet(PFE_Cfg("EnableRememberPathHistory"),"1","2"))
	{
		UpdateCurrentPathContext();
		auto &vec=PFE_Cfg["AllPathContext"];
		vec.clear();
		if (!AllPathContext.empty())
			vec.push_back(llTOstr(CurrentPathContextPos));
		for (int i=0;i<AllPathContext.size();++i)
		{
			vec.push_back("###");
			PathContext_Encode(*AllPathContext[i],vec.back());
		}
		
		{
			auto &vec=PFE_Cfg["RecentList"];
			vec.clear();
			if (PFE_Cfg("EnableRecentListGradualColor")=="1")
				for (int i=RecentListLRULimit;i>=0;--i)//Efficiency??
					for (int j=0;j<LVT_RecentList->GetListCnt();++j)
					{
						LVT_QuickListData &data=LVT_RecentList->GetFuncData(j);
						if (data.NumCode!=i)
							continue;
						vec.push_back(llTOstr(data.path.type));
						vec.push_back(data.path.str);
						vec.push_back(data.path.name);
						vec.push_back(llTOstr(data.path.code));
					}
			else
				for (int i=0;i<LVT_RecentList->GetListCnt();++i)
				{
					LVT_QuickListData &data=LVT_RecentList->GetFuncData(i);
					vec.push_back(llTOstr(data.path.type));
					vec.push_back(data.path.str);
					vec.push_back(data.path.name);
					vec.push_back(llTOstr(data.path.code));
				}
		}
	}
	DD[5]<<"SavePathHistoryData"<<endl;
}

void ReadPathHistoryData()
{
	DD[4]<<"ReadPathHistoryData"<<endl;
	string s;
	if (strISull(s=PFE_Cfg("AllPathContext")))
	{
		auto &vec=PFE_Cfg["AllPathContext"];
		CurrentPathContextPos=strTOll(vec[0]);
		for (int i=1;i<vec.size();++i)
		{
			PathContext *pc=new PathContext();
			PathContext_Decode(vec[i],*pc);
			AllPathContext.push_back(pc);
		}
		if (!AllPathContext.empty())
		{
			CurrentPathContextPos=EnsureInRange(CurrentPathContextPos,0,(int)AllPathContext.size()-1);
			CurrentPathContext=AllPathContext[CurrentPathContextPos];
		}
	}
	
	{
		auto &vec=PFE_Cfg["RecentList"];
		bool enablerecentlistgradualcolor=PFE_Cfg("EnableRecentListGradualColor")=="1";
		for (int i=3,j=0;i<vec.size();i+=4,++j)
			SetLeftListItem(enablerecentlistgradualcolor?0:j,PFE_Path(strTOll(vec[i-3]),vec[i-2],vec[i-1],strTOll(vec[i])),1);
	}
	DD[5]<<"ReadPathHistoryData"<<endl;
}

vector <string> StartWithOpenedPathTab;
void InitTabView(int choice=0)//choice 0:init 1:restore 2:cancel
{
	DD[4]<<"InitTabView"<<endl;
	string s;
	if (choice==0&&PFE_Cfg("EnableRememberPathHistory")=="2"&&strISull(s=PFE_Cfg("AllPathContext")))
	{
		auto msgbx=new MessageBoxButtonI(0,PUIT("提示"),PUIT("上次关闭时的有打开的路径，是否恢复？"));
		msgbx->SetBackgroundColor(DUC_MessageLayerBackground);
		msgbx->SetTopAreaColor(DUC_MessageLayerTopAreaColor);
		msgbx->AddButton(PUIT("恢复"),[](int&){InitTabView(1);},0);
		msgbx->AddButton(PUIT("取消"),[](int&){InitTabView(2);},0);
		return;
	}
	else if (choice==1||choice==0&&PFE_Cfg("EnableRememberPathHistory")=="1")
		ReadPathHistoryData();
	
	int tabMode=0;
	if (strISull(s=PFE_Cfg("CurrentTabMode")))
		tabMode=strTOll(s);
	
	if (!StartWithOpenedPathTab.empty())
		if (tabMode!=0)
		{
			for (const auto &vp:StartWithOpenedPathTab)
			{
				PFE_Path path=PathStrToPFEPath(vp);
				if (NotInSet(path.type,PFE_PathType_Dir,PFE_PathType_Volume))
					continue;
				AllPathContext.push_back(new PathContext(path));
			}
			if (!AllPathContext.empty())
			{
				CurrentPathContextPos=(int)AllPathContext.size()-1;
				CurrentPathContext=AllPathContext[CurrentPathContextPos];
			}
		}
		else if (CurrentPathContext==nullptr)
		{
			PFE_Path path=PathStrToPFEPath(StartWithOpenedPathTab[0]);
			if (InThisSet(path.type,PFE_PathType_Dir,PFE_PathType_Volume))
			{
				CurrentPathContext=new PathContext(path);
				AllPathContext.push_back(CurrentPathContext);
				CurrentPathContextPos=0;
			}
		}
	
	if (CurrentPathContext==nullptr)
	{
		CurrentPathContext=new PathContext(PFE_Path_MainPage);
		AllPathContext.push_back(CurrentPathContext);
		CurrentPathContextPos=0;
	}
	CurrentPath=CurrentPathContext->CurrentPathWithExtraData.path;
	
	SwitchTabMode(tabMode);
	
	if (CurrentPath==PFE_Path())//??
		SetCurrentPath(PFE_Path_MainPage,SetCurrentPathFrom_Refresh);
	else SetCurrentPath(CurrentPath,SetCurrentPathFrom_TabView);
	DD[5]<<"InitTabView"<<endl;
}
//End of TabView

//AutoRefresh Area
const unsigned UserEvent_AutoRefresh=PUI_Event::RegisterEvent();
unsigned AutoRefreshMinInterval=100;
atomic_bool MonitorChangeThreadQuitFlag(0);
PAL_Thread <int,int> *Th_MonitorChange=nullptr;
PAL_Mutex *Mu_MonitorChange=nullptr;
wstring CurrentMonitorChangePath;
HANDLE MonitorChangeEventHandle=NULL;

int ThreadFunc_MonitorChange(int&)
{
	HANDLE h=NULL;
	unsigned LastSendEventTick=0;
	bool HaveSkipedEvent=0;
	while (!MonitorChangeThreadQuitFlag)
	{
		if (h!=NULL)
		{
			HANDLE handles[]={MonitorChangeEventHandle,h};
			switch (WaitForMultipleObjects(sizeof(handles)/sizeof(HANDLE),handles,FALSE,HaveSkipedEvent?AutoRefreshMinInterval:INFINITE))
			{
				case WAIT_OBJECT_0:	break;
				case WAIT_TIMEOUT:
					HaveSkipedEvent=0;
					PUI_SendUserEvent(UserEvent_AutoRefresh);
					continue;
				case WAIT_OBJECT_0+1:
				{
					unsigned tick=SDL_GetTicks();
					if (LastSendEventTick+AutoRefreshMinInterval<=tick)
						LastSendEventTick=tick,
						PUI_SendUserEvent(UserEvent_AutoRefresh);
					else HaveSkipedEvent=1;
					FindNextChangeNotification(h);
					continue;
				}
			}
		}
		else WaitForSingleObject(MonitorChangeEventHandle,INFINITE);
		if (MonitorChangeThreadQuitFlag)
			break;
		Mu_MonitorChange->Lock();
		if (h!=NULL)
			FindCloseChangeNotification(h);
		h=NULL;
		if (CurrentMonitorChangePath!=L"")
			h=FindFirstChangeNotificationW(CurrentMonitorChangePath.c_str(),0,FILE_NOTIFY_CHANGE_DIR_NAME|FILE_NOTIFY_CHANGE_FILE_NAME|FILE_NOTIFY_CHANGE_SIZE);
		if (h==INVALID_HANDLE_VALUE)
			h=NULL;
		Mu_MonitorChange->Unlock();
	}
	if (h!=NULL)
		FindCloseChangeNotification(h);
	return 0;
}

void InitMonitorChangeThread()
{
	DD[4]<<"InitMonitorChangeThread"<<endl;
	Mu_MonitorChange=new PAL_Mutex();
	MonitorChangeEventHandle=CreateEventW(NULL,0,0,NULL);
	Th_MonitorChange=new PAL_Thread<int,int> (ThreadFunc_MonitorChange,0);
	DD[5]<<"InitMonitorChangeThread"<<endl;
}

void SetDirectoryMonitorTarget(const PFE_Path &path)
{
	Mu_MonitorChange->Lock();
	if (InThisSet(path.type,PFE_PathType_Dir,PFE_PathType_Volume))
		CurrentMonitorChangePath=DeleteEndBlank(Utf8ToUnicode(path.str));
	else CurrentMonitorChangePath=L"";
	Mu_MonitorChange->Unlock();
	SetEvent(MonitorChangeEventHandle);
}

void QuitMonitorChangeThread()
{
	DD[4]<<"QuitMonitorChangeThread"<<endl;
	SetDirectoryMonitorTarget(PFE_Path());
	MonitorChangeThreadQuitFlag=1;
	delete Th_MonitorChange;
	delete Mu_MonitorChange;
	CloseHandle(MonitorChangeEventHandle);
	DD[5]<<"QuitMonitorChangeThread"<<endl;
}
//End of AutoRefresh Area 

//FileOperation Area 
enum
{
	FileOperationType_None,
	FileOperationType_Copy,//Perform in thread
	FileOperationType_Move,//Presolve try directly MoveFile(),if failed,enter thread.
//	FileOperationType_Rename,//Presolve
	FileOperationType_Delete,//Perform in thread
	FileOperationType_Recycle//Same with MoveFile(),but always in same volume, so always Presolve
};

enum
{
	FileOperationMode_None,
	FileOperationMode_Queue,
//	FileOperationMode_GroupParallel,
	FileOperationMode_TaskParallel
};
	
struct FileOperationStep
{
	enum
	{
		Type_None,
		Type_SelectSrc,
		Type_Enter,
		Type_Leave,
		Type_File,
		Type_End
	};
	
	int type=0;
	union
	{
		long long Code;//Common Access
		long long SrcIndex;//Type_SwitchSrc
		long long Size;//Type_File
		long long PairIndex;//Type_Enter(>0),Type_Leave(<0)
	};
	unsigned long long size=0;//if Type_SwitchSrc,IsDir is used as index,else if not Type_Normal, always 0
	wstring name;
	
	FileOperationStep(int _type,long long _code,const wstring &_name):type(_type),Code(_code),name(_name) {}
	
	FileOperationStep() {}
};

struct FileOperationTask
{
	const vector <string> Src;//bool:IsDir
	const string Dst;//Dst is always a directory
	const int OperationType=0,
			  Index=0;
	
	SDL_mutex *Mu=NULL;//Protect this struct
	queue <FileOperationStep> StepList;
	wstring CurrentStepName;
	//Filter;

	int RememberedExceptionAction_OverrideFile=0,//0:none 1:override 2:skip 3:newest 4:rename 5:cancel
		RememberedExceptionAction_MergeDir=0,//0:none 1:merge 2:skip 3:rename 4:cancel
		RememberedExceptionAction_GetPrivilege=0,//0:none 1:retry(admin) 2:retry 3:skip 4:cancel
		RememberedExceptionAction_RetryCopyMoveFile=0,//0:none 1:retry 2:skip 3:cancel
		RememberedExceptionAction_RetryCreateDir=0,//0:none 1:retry 2:skip 3:cancel
		RememberedExceptionAction_RetryRemoveDir=0,//0:none 1:retry 2:skip 3:cancel
		RememberedExceptionAction_RetryRemoveFile=0;//0:none 1:retry 2:skip 3:cancel
		
	queue <Doublet<unsigned long long,unsigned long long> > ShortTermStateQue;
	unsigned long long LastBaseTime=0,
					   LastCompletedSize=0;
	atomic_ullong GlobalCopyTime,
				  ShortTermCopySize,
				  ShortTermCopyTime;
				  
	atomic_ullong TotalItem,
				  CompletedItem,
				  TotalSize,
				  CompletedSize,
				  CurrentItemSolvedSize,
				  CurrentItemTotalSize;
	SDL_Thread *CurrentThread=NULL,
			   *CurrentSearchingThread=NULL;
	atomic_int CurrentState;//0:Not start 1:Running(Calculating Size) 2:Running(Calculating OK) 3:Completed
	//Allocator scan this flag, if completed(2), resources will be retrieved.
	atomic_int PauseOrCancel;//0:None 1:Pause 2:Cancel
	
	///Although these data is not protect or private, it is recommended to use avaliable functions to access them for safety.
	inline void PushStep(const FileOperationStep &step)
	{
		SDL_LockMutex(Mu);
		StepList.push(step);
		SDL_UnlockMutex(Mu);
	}
	
	inline void PushStep(int type,unsigned long long size,const wstring &name)
	{
		SDL_LockMutex(Mu);
		StepList.push(FileOperationStep(type,size,name));
		SDL_UnlockMutex(Mu);
	}
	
	inline bool GetStep(FileOperationStep &step)
	{
		bool re=0;
		SDL_LockMutex(Mu);
		if (!StepList.empty())
		{
			re=1;
			step=StepList.front();
			CurrentStepName=step.name;//??
			CurrentItemSolvedSize=0;
			CurrentItemTotalSize=step.type==FileOperationStep::Type_File?step.Size:0;
			StepList.pop();
		}
		SDL_UnlockMutex(Mu);
		return re;
	}
	
	inline string GetCurrentStepName()
	{
		string re;
		SDL_LockMutex(Mu);
		re=DeleteEndBlank(UnicodeToUtf8(CurrentStepName));
		SDL_UnlockMutex(Mu);
		return re;
	}
	
	inline void Cancel()
	{PauseOrCancel=2;}
	
	inline double GetPercent()
	{return TotalSize==0?(TotalItem==0?0:CompletedItem*1.0/TotalItem):(CompletedSize+CurrentItemSolvedSize)*1.0/TotalSize;}
	
	inline void UpdateBaseTimeAndSize()//Start measure
	{
		LastBaseTime=SDL_GetPerformanceCounter();
		LastCompletedSize=CompletedSize+CurrentItemSolvedSize;
	}
	
	inline void UpdateCopiedSize()//End short term measure
	{
		while (ShortTermCopyTime>=SDL_GetPerformanceFrequency()*3.0)//??
			ShortTermCopySize-=ShortTermStateQue.front().a,
			ShortTermCopyTime-=ShortTermStateQue.front().b,
			ShortTermStateQue.pop();
		unsigned long long CurrentTimeCode=SDL_GetPerformanceCounter();
		ShortTermStateQue.push({CompletedSize+CurrentItemSolvedSize-LastCompletedSize,CurrentTimeCode-LastBaseTime});
		LastCompletedSize=CompletedSize+CurrentItemSolvedSize;
		LastBaseTime=CurrentTimeCode;
		GlobalCopyTime+=ShortTermStateQue.back().b;
		ShortTermCopyTime+=ShortTermStateQue.back().b;
		ShortTermCopySize+=ShortTermStateQue.back().a;
	}
	
	inline unsigned long long GetSpeed()//per second
	{
		if (ShortTermCopyTime==0)
			return 0;
		else return ShortTermCopySize*1.0*SDL_GetPerformanceFrequency()/ShortTermCopyTime;
	}
	
	inline unsigned long long GetGlobalSpeed()//per second
	{
		if (GlobalCopyTime==0)
			return 0;
		else return (CompletedSize+CurrentItemSolvedSize)*1.0*SDL_GetPerformanceFrequency()/GlobalCopyTime;
	}
	
	inline void Pause(bool pause)
	{
		if (PauseOrCancel!=2)
			PauseOrCancel=pause;
	}
	
	FileOperationTask(const vector <string> &src,const string &dst,int operationtype,int index):Src(src),Dst(dst),OperationType(operationtype),Index(index)
	{
		TotalItem=0;
		CompletedItem=0;
		TotalSize=0;
		CompletedSize=0;
		CurrentItemSolvedSize=0;
		CurrentItemTotalSize=0;
		CurrentState=0;
		PauseOrCancel=0;
		GlobalCopyTime=0;
		ShortTermCopySize=0;
		ShortTermCopyTime=0;
	}
};

struct FileOperationTaskQuestion
{
	int index=0;
	string QuestionTitle;
	vector <Doublet<int,string> > ChoiceName;//avalible choices number at present:1~6, more than 7 may unable to display...
	//the choice button size is non-increasing,that means the previous choice is more important
	bool RememberChoice=0;
};

atomic_bool FileOperationQuitFlag(0);

void FileOperationFunc_Search(FileOperationTask *task,const wstring &basePath,int &pairingIndexAssign)
{
	long long hFiles=0;
	_wfinddatai64_t da;
	if ((hFiles=_wfindfirsti64((basePath+L"\\*").c_str(),&da))!=-1)
		do
		{
			if (wcscmp(da.name,L".")!=0&&wcscmp(da.name,L"..")!=0)
				if (da.attrib&_A_SUBDIR)
				{
					++task->TotalItem;
					int pairIndex=++pairingIndexAssign;
					task->PushStep(FileOperationStep::Type_Enter,pairIndex,da.name);
					FileOperationFunc_Search(task,basePath+L"\\"+da.name,pairingIndexAssign);
					task->PushStep(FileOperationStep::Type_Leave,-pairIndex,da.name);
				}
				else
				{
					++task->TotalItem;
					task->TotalSize+=da.size;
					task->PushStep(FileOperationStep::Type_File,da.size,da.name);
				}
			while (!FileOperationQuitFlag&&task->PauseOrCancel==1)
				SDL_Delay(10);
		}
		while (!FileOperationQuitFlag&&task->PauseOrCancel!=2&&_wfindnexti64(hFiles,&da)==0);
	_findclose(hFiles);
}

DWORD CALLBACK FileOperation_ProgressRoutine(LARGE_INTEGER TotalFileSize,LARGE_INTEGER TotalBytesTransferred,LARGE_INTEGER StreamSize,LARGE_INTEGER StreamBytesTransferred,
								DWORD dwStreamNumber,DWORD dwCallbackReason,HANDLE hSourceFile,HANDLE hDestinationFile,LPVOID lpData)
{
	FileOperationTask *task=(FileOperationTask*)lpData;
	task->CurrentItemSolvedSize=TotalBytesTransferred.QuadPart;
	task->UpdateCopiedSize();
	
	while (!FileOperationQuitFlag&&task->PauseOrCancel==1)
		SDL_Delay(10);
	
	task->UpdateBaseTimeAndSize();
	if (FileOperationQuitFlag||task->PauseOrCancel==2)
		return PROGRESS_CANCEL;
	else return PROGRESS_CONTINUE;
}

inline bool CompareNewestFile(const wstring &path1,const wstring &path2,int timeType=0)//return 1 if path1 is newer than path2;if error,always return 0 (??)
{
	bool re=0;
	long long hFiles1=0,hFiles2=0;
	_wfinddatai64_t da1,da2;
	if ((hFiles1=_wfindfirsti64(path1.c_str(),&da1))!=-1&&(hFiles2=_wfindfirsti64(path2.c_str(),&da2))!=-1)
	{
		switch (timeType)
		{
			default:
			case 0:	re=da1.time_write>da2.time_write; 	break;
			case 1:	re=da1.time_create>da2.time_create;	break;
			case 2:	re=da1.time_access>da2.time_access;	break;
		}
	}
	_findclose(hFiles1);
	_findclose(hFiles2);
	return re;
}

void SetFileOperationTaskQuestion(SynchronizeFunctionDataT <FileOperationTaskQuestion>);
inline int WaitFileOperationTaskQuestion(FileOperationTaskQuestion &q)
{
	return PUI_SendSynchronizeFunctionEvent<FileOperationTaskQuestion>([](SynchronizeFunctionDataT <FileOperationTaskQuestion> &controller)
	{
		SetFileOperationTaskQuestion(controller);
	},q);
}

void FileOperationFunc_Operation(FileOperationTask *task)
{
	FileOperationStep step;
	wstring src,dst=DeleteEndBlank(Utf8ToUnicode(task->Dst));
	vector <Doublet<wstring,wstring> > PrefixPathStack;
	PrefixPathStack.push_back({L"",L""});
	int SkipTo=0;
//	int MaxSkipDepth=0;
	while (!FileOperationQuitFlag&&task->PauseOrCancel!=2)
		if (task->GetStep(step))//Generally,the search is faster than operation, so I don't need such as semaphore to wait in case of busy wait.
		{
//			DD[3]<<"Operating"<<endl;
			if (SkipTo!=0)
			{
				if (InThisSet(step.type,FileOperationStep::Type_File,FileOperationStep::Type_Leave))
					++task->CompletedItem;
				if (step.type==FileOperationStep::Type_File)
					task->CompletedSize+=step.Size;
				if (step.type==FileOperationStep::Type_Leave&&step.PairIndex==SkipTo)
				{
					SkipTo=0;
					PrefixPathStack.pop_back();
				}
			}
			else switch (step.type)
			{
				case FileOperationStep::Type_SelectSrc:
					if (PrefixPathStack.size()!=1)
					{
						PUI_SendFunctionEvent <int> ([](int &index)
						{
							DD[2]<<"PerformFileOperation operate error! Step PrefixPathStack of "<<index<<" structure error!"<<endl;
							WarningMessageButton(SetSystemBeep_Error,PUIT("错误"),PUIT("任务 "+llTOstr(index)+" 操作步骤发生了结构性错误!"),PUIT("中止"));
						},task->Index);
						task->Cancel();
					}
					src=step.name;
					break;
				case FileOperationStep::Type_Enter:
					PrefixPathStack.push_back({PrefixPathStack.back().a+L"\\"+step.name,PrefixPathStack.back().b+L"\\"+step.name});
					if (InThisSet(task->OperationType,FileOperationType_Copy,FileOperationType_Move,FileOperationType_Recycle))
					{
						wstring dstPath=dst+PrefixPathStack.back().b;
						while (!FileOperationQuitFlag&&task->PauseOrCancel!=2)
							if (IsFileExsit(dstPath,1))
							{
								int x=0;
								if (task->RememberedExceptionAction_MergeDir==0)
								{
									FileOperationTaskQuestion q;
									q.index=task->Index;
									q.QuestionTitle=PUIT("文件夹已存在: ")+DeleteEndBlank(UnicodeToUtf8(dstPath));
									q.ChoiceName.push_back({1,PUIT("合并")});
									q.ChoiceName.push_back({2,PUIT("跳过")});
									q.ChoiceName.push_back({3,PUIT("重命名")});
									q.ChoiceName.push_back({4,PUIT("取消")});
									x=WaitFileOperationTaskQuestion(q);
									if (q.RememberChoice)
										task->RememberedExceptionAction_MergeDir=x;
								}
								else x=task->RememberedExceptionAction_MergeDir;
								if (x==1)
									break;
								else if (x==2)
								{
									SkipTo=-step.PairIndex;
									break;
								}
								else if (x==3)
								{
									PrefixPathStack.back().b+=L" (new)";
									dstPath=dst+PrefixPathStack.back().b;
									continue;
								}
								else
								{
									task->Cancel();
									break;
								}
							}
							else if (!CreateDirectoryW(dstPath.c_str(),NULL))
							{
								int x=0;
								if (task->RememberedExceptionAction_RetryCreateDir==0)
								{
									FileOperationTaskQuestion q;
									q.index=task->Index;
									q.QuestionTitle=PUIT("无法创建文件夹: ")+DeleteEndBlank(UnicodeToUtf8(dstPath));
									q.ChoiceName.push_back({1,PUIT("重试")});
									q.ChoiceName.push_back({2,PUIT("跳过")});
									q.ChoiceName.push_back({3,PUIT("取消")});
									x=WaitFileOperationTaskQuestion(q);
									if (q.RememberChoice)
										task->RememberedExceptionAction_RetryCreateDir=x;
								}
								else x=task->RememberedExceptionAction_RetryCreateDir;
								if (x==1)
									continue;
								else if (x==2)
								{
									SkipTo=-step.PairIndex;
									break;
								}
								else
								{
									task->Cancel();
									break;
								}
							}
							else break;
					}
					break;
				case FileOperationStep::Type_Leave:
					if (InThisSet(task->OperationType,FileOperationType_Move,FileOperationType_Delete,FileOperationType_Recycle))
					{
						wstring srcPath=src+PrefixPathStack.back().a;
						while (!FileOperationQuitFlag&&task->PauseOrCancel!=2)
							if (!RemoveDirectoryW(srcPath.c_str()))
							{
								int x=0;
								if (task->RememberedExceptionAction_RetryRemoveDir==0)
								{
									FileOperationTaskQuestion q;
									q.index=task->Index;
									q.QuestionTitle=PUIT("无法删除文件夹: ")+DeleteEndBlank(UnicodeToUtf8(srcPath));
									q.ChoiceName.push_back({1,PUIT("重试")});
									q.ChoiceName.push_back({2,PUIT("跳过")});
									q.ChoiceName.push_back({3,PUIT("取消")});
									x=WaitFileOperationTaskQuestion(q);
									if (q.RememberChoice)
										task->RememberedExceptionAction_RetryRemoveDir=x;
								}
								else x=task->RememberedExceptionAction_RetryRemoveDir;
								if (x==1)
									continue;
								else if (x==2)
									break;
								else
								{
									task->Cancel();
									break;
								}
							}
							else break;
					}
					++task->CompletedItem;//??
					PrefixPathStack.pop_back();
					break;
				case FileOperationStep::Type_File:
				{
					wstring srcPath=src+PrefixPathStack.back().a+L"\\"+step.name,
							dstPath=dst+PrefixPathStack.back().b+L"\\"+step.name;
					if (InThisSet(task->OperationType,FileOperationType_Copy,FileOperationType_Move,FileOperationType_Recycle))
						while (!FileOperationQuitFlag&&task->PauseOrCancel!=2)
						{
							bool Override=0;
							if (IsFileExsit(dstPath,0))
							{
								int x=0;
								if (task->RememberedExceptionAction_OverrideFile==0)
								{
									FileOperationTaskQuestion q;
									q.index=task->Index;
									q.QuestionTitle=PUIT("文件已存在: ")+DeleteEndBlank(UnicodeToUtf8(dstPath));
									q.ChoiceName.push_back({1,PUIT("覆盖")});
									q.ChoiceName.push_back({2,PUIT("跳过")});
									q.ChoiceName.push_back({3,PUIT("保留最新")});
									q.ChoiceName.push_back({4,PUIT("重命名")});
									q.ChoiceName.push_back({5,PUIT("取消")});
									x=WaitFileOperationTaskQuestion(q);
									if (q.RememberChoice)
										task->RememberedExceptionAction_OverrideFile=x;
								}
								else x=task->RememberedExceptionAction_OverrideFile;
								if (x==1)
									Override=1;
								else if (x==2)
									break;
								else if (x==3)
									if (CompareNewestFile(srcPath,dstPath))
										Override=1;
									else break;
								else if (x==4)
								{
									auto newFilename=[](const wstring &wstr)->wstring
									{
										wstring re;
										int i;
										for (i=(int)wstr.length()-1;i>=0;--i)
											if (wstr[i]==L'.')
												break;
										if (i==-1)
											return wstr+L" (new)";
										else return wstr.substr(0,i)+L" (new)"+wstr.substr(i,wstr.length()-i);
									};
									dstPath=newFilename(dstPath);
									continue;
								}
								else
								{
									task->Cancel();
									break;
								}
							}
							
							task->UpdateBaseTimeAndSize();
							int y=task->OperationType==FileOperationType_Copy
								?CopyFileExW(srcPath.c_str(),dstPath.c_str(),FileOperation_ProgressRoutine,task,NULL,Override?0:COPY_FILE_FAIL_IF_EXISTS/*??*/)
								:MoveFileWithProgressW(srcPath.c_str(),dstPath.c_str(),FileOperation_ProgressRoutine,task,MOVEFILE_COPY_ALLOWED|(Override?MOVEFILE_REPLACE_EXISTING:0));
							task->UpdateCopiedSize();
							if (y==0&&GetLastError()==5)
							{
								int x=0;
								if (task->RememberedExceptionAction_GetPrivilege==0)
								{
									FileOperationTaskQuestion q;
									q.index=task->Index;
									q.QuestionTitle=PUIT("拒绝访问: ")+DeleteEndBlank(UnicodeToUtf8(dstPath));
									q.ChoiceName.push_back({1,PUIT("以管理员身份重试(暂不可用)")});
									q.ChoiceName.push_back({2,PUIT("重试")});
									q.ChoiceName.push_back({3,PUIT("跳过")});
									q.ChoiceName.push_back({4,PUIT("取消")});
									x=WaitFileOperationTaskQuestion(q);
									if (q.RememberChoice)
										task->RememberedExceptionAction_GetPrivilege=x;
								}
								else x=task->RememberedExceptionAction_GetPrivilege;
								if (x==1)
								{
//									PAL_Platform::AdjustPrivilege();
									continue;
								}
								else if (x==2)
									continue;
								else if (x==3)
									break;
								else
								{
									task->Cancel();
									break;
								}
							}
							else if (y==0)
							{
								int x=0;
								if (task->RememberedExceptionAction_RetryCopyMoveFile==0)
								{
									FileOperationTaskQuestion q;
									q.index=task->Index;
									q.QuestionTitle=PUIT("复制/移动/回收失败: ")+DeleteEndBlank(UnicodeToUtf8(dstPath));
									q.ChoiceName.push_back({1,PUIT("重试")});
									q.ChoiceName.push_back({2,PUIT("跳过")});
									q.ChoiceName.push_back({3,PUIT("取消")});
									x=WaitFileOperationTaskQuestion(q);
									if (q.RememberChoice)
										task->RememberedExceptionAction_RetryCopyMoveFile=x;
								}
								else x=task->RememberedExceptionAction_RetryCopyMoveFile;
								if (x==1)
									continue;
								else if (x==2)
									break;
								else
								{
									task->Cancel();
									break;
								}
							}
							else break;
						}
					else if (task->OperationType==FileOperationType_Delete)
						while (!FileOperationQuitFlag&&task->PauseOrCancel!=2)
							if (!DeleteFileW(srcPath.c_str()))
							{
								int x=0;
								if (task->RememberedExceptionAction_RetryRemoveFile==0)
								{
									FileOperationTaskQuestion q;
									q.index=task->Index;
									q.QuestionTitle=PUIT("无法删除文件: ")+DeleteEndBlank(UnicodeToUtf8(srcPath));
									q.ChoiceName.push_back({1,PUIT("重试")});
									q.ChoiceName.push_back({2,PUIT("跳过")});
									q.ChoiceName.push_back({3,PUIT("取消")});
									x=WaitFileOperationTaskQuestion(q);
									if (q.RememberChoice)
										task->RememberedExceptionAction_RetryRemoveFile=x;
								}
								else x=task->RememberedExceptionAction_RetryRemoveFile;
								if (x==1)
									continue;
								else if (x==2)
									break;
								else
								{
									task->Cancel();
									break;
								}
							}
							else break;
					else PUI_SendFunctionEvent <Doublet<int,int> > ([](Doublet <int,int> &data)
					{
						DD[2]<<"PerformFileOperation operate error! FileOperationType "<<data.b<<" of task "<<data.a<<" is invalid!"<<endl;
						WarningMessageButton(SetSystemBeep_Error,PUIT("错误"),PUIT("任务 "+llTOstr(data.a)+" 的操作类型设置无效！"),PUIT("确定"));
					},{task->Index,task->OperationType});
					++task->CompletedItem;
					task->CompletedSize+=step.Size;
					break;
				}
				case FileOperationStep::Type_End:
					return;//??
				default:
					//Error:Wrong FileOperationStepType
					break;
			}
			
			while (!FileOperationQuitFlag&&task->PauseOrCancel==1)
				SDL_Delay(10);
		}
		else if (task->CurrentState==1)
			SDL_Delay(1);
		else break;
}

void FileOperationTaskAllocator(int,FileOperationTask*);

void PerformFileOperation(FileOperationTask *task)
{
//	DD[3]<<"PerformOperation start"<<endl;
	task->CurrentSearchingThread=SDL_CreateThread([](void *data)->int
	{
		FileOperationTask *task=(FileOperationTask*)data;
		int pairingIndexAssign=0;
		for (size_t i=0;i<task->Src.size();++i)
		{
			wstring basepath=DeleteEndBlank(Utf8ToUnicode(task->Src[i]));
			task->PushStep(FileOperationStep::Type_SelectSrc,i,DeleteEndBlank(Utf8ToUnicode(GetPreviousBeforeBackSlash(task->Src[i]))));//???
			long long hFiles=0;
			_wfinddatai64_t da;
			if ((hFiles=_wfindfirsti64(basepath.c_str(),&da))!=-1)
				if (da.attrib&_A_SUBDIR)
				{
					++task->TotalItem;
					int pairIndex=++pairingIndexAssign;
					task->PushStep(FileOperationStep::Type_Enter,pairIndex,da.name);
					FileOperationFunc_Search(task,basepath,pairingIndexAssign);
					task->PushStep(FileOperationStep::Type_Leave,-pairIndex,da.name);
				}
				else
				{
					++task->TotalItem;
					task->TotalSize+=da.size;
					task->PushStep(FileOperationStep::Type_File,da.size,da.name);
				}
			else PUI_SendFunctionEvent<string>([](string &path)
			{
				DD[2]<<"PerformFileOperation search error! SrcPath "<<path<<" is invalid!"<<endl;
				WarningMessageButton(SetSystemBeep_Error,PUIT("错误"),path+PUIT(" 不存在！"),PUIT("跳过"));
			},task->Src[i]);
			_findclose(hFiles);
			if (FileOperationQuitFlag)
				return 1;
		}
		task->PushStep(FileOperationStep::Type_End,0,L"");
		task->CurrentState=2;
//		DD[3]<<"FileOperation SearchOK"<<endl;
		return 0;
	},"FileOperationSearchThread",task);

	FileOperationFunc_Operation(task);
	SDL_WaitThread(task->CurrentSearchingThread,NULL);
	task->CurrentSearchingThread=NULL;
	task->CurrentState=3;
	
	if (!FileOperationQuitFlag)
		PUI_SendFunctionEvent<FileOperationTask*>([](FileOperationTask *&task)
		{
			FileOperationTaskAllocator(2,task);
		},task);
}

void ThreadPerformFileOperation(FileOperationTask *task)
{
	task->CurrentThread=SDL_CreateThread([](void *data)->int
	{
		PerformFileOperation((FileOperationTask*)data);
		return 0;
	},"FileOperationThread",task);
}

ButtonI *Bu_OpenFileOperationPopupPanel=NULL;
ProgressBar *ProBar_FileOperationGeneralProgress=NULL;
MessageBoxLayer *MBL_FileOperationPanel=NULL;
ListViewTemplate <int> *LVT_FileOperationPanels=NULL;
struct FileOperationPanel
{
	const int index=0;
	FileOperationTask * const task=NULL;
	int UIState=0;//0:None 1:Progress 2:Question(Hidden some of Progress Widgets)
	SynchronizeFunctionDataT <FileOperationTaskQuestion> controller;
	
	Widgets *BaseLayer=NULL;
	BorderRectLayer *Border=NULL;
	TinyText *TT_Title=NULL,
			 *TT_State=NULL,
			 *TT_Speed=NULL,
			 *TT_CurrentName=NULL,
			 *TT_LeftTime=NULL,
			 *TT_LeftItems=NULL;
	ProgressBar *ProBar=NULL;
	Button <FileOperationPanel*> *Pause=NULL,
								 *Cancel=NULL;
	Layer *Lay_QuestionButton=NULL;
	CheckBox <FileOperationPanel*> *CB_RememberChoice=NULL;
	TinyText *TT_RememberChoiceTip=NULL;
	
	void CloseQuestionUI()
	{
		if (UIState!=2)
		{
			DD[2]<<"FileOperationPanel invalid operation: CloseQuestionUI when UIState is "<<UIState<<"!"<<endl;
			return;
		}
		
		DelayDeleteToNULL(Lay_QuestionButton);
		DelayDeleteToNULL(CB_RememberChoice);
		DelayDeleteToNULL(TT_RememberChoiceTip);
		TT_State->SetText("");
		Border->SetBorderColor(RGBA_NONE);
		
		TT_Speed->SetEnabled(1);
		TT_CurrentName->SetEnabled(1);
		TT_LeftTime->SetEnabled(1);
		TT_LeftItems->SetEnabled(1);
		ProBar->SetEnabled(1);
		Pause->SetEnabled(1);
		Cancel->SetEnabled(1);
		
		UIState=1;
	}
	
	void ShowQuestionUI()
	{
		if (UIState!=1)
		{
			DD[2]<<"FileOperationPanel invalid operation: ShowQuestionUI when UIState is "<<UIState<<"!"<<endl;
			return;
		}
		if (!controller.Valid())
		{
			DD[2]<<"FileOperationPanel invalid operation: ShowQuestionUI while controller is invalid!"<<endl;
			return;
		}
		
		Border->SetBorderColor(RGBA_RED);
		TT_Speed->SetEnabled(0);
		TT_CurrentName->SetEnabled(0);
		TT_LeftTime->SetEnabled(0);
		TT_LeftItems->SetEnabled(0);
		ProBar->SetEnabled(0);
		Pause->SetEnabled(0);
		Cancel->SetEnabled(0);
		
		TT_State->SetText(controller->QuestionTitle);
		Lay_QuestionButton=new Layer(0,Border,new PosizeEX_Fa6(2,2,30,30,65,40));
		auto NewButton=[this](PosizeEX_Fa6 *psex,int pos)
		{
			new Button <Doublet<FileOperationPanel*,int> > (0,Lay_QuestionButton,psex,controller->ChoiceName[pos-1].b,
				[](Doublet<FileOperationPanel*,int> &data)
				{
					data.a->controller.Continue(data.b);
					data.a->CloseQuestionUI();
				},{this,controller->ChoiceName[pos-1].a});
		};
		
		if (!InRange(controller->ChoiceName.size(),1,6))
			DD[2]<<"FileOperationPanel QuestionUI parameter error! ChoiceName size is "<<controller->ChoiceName.size()<<endl;
		if (InRange(controller->ChoiceName.size(),1,6))
			NewButton(new PosizeEX_Fa6(2,3,0,0,0,-0.32),1);
		if (InRange(controller->ChoiceName.size(),2,4))
			NewButton(new PosizeEX_Fa6(2,3,0,0,-0.34,-0.32),2);
		else if (InRange(controller->ChoiceName.size(),5,6))
			NewButton(new PosizeEX_Fa6(3,3,0,-0.617,-0.34,-0.32),2);
		if (controller->ChoiceName.size()==3)
			NewButton(new PosizeEX_Fa6(2,3,0,0,-0.68,-0.32),3);
		else if (InRange(controller->ChoiceName.size(),4,5))
			NewButton(new PosizeEX_Fa6(3,3,0,-0.617,-0.68,-0.32),3);
		else if (controller->ChoiceName.size()==6)
			NewButton(new PosizeEX_Fa6(1,3,-0.381,0,-0.34,-0.32),3);
		if (controller->ChoiceName.size()==4)
			NewButton(new PosizeEX_Fa6(1,3,-0.381,0,-0.68,-0.32),4);
		else if (controller->ChoiceName.size()==5)
			NewButton(new PosizeEX_Fa6(1,3,-0.381,0,-0.34,-0.32),4);
		else if (controller->ChoiceName.size()==6)
			NewButton(new PosizeEX_Fa6(3,3,0,-0.32,-0.68,-0.32),4);
		if (controller->ChoiceName.size()==5)
			NewButton(new PosizeEX_Fa6(1,3,-0.381,0,-0.68,-0.32),5);
		else if (controller->ChoiceName.size()==6)
			NewButton(new PosizeEX_Fa6(3,3,-0.34,-0.32,-0.68,-0.32),5);
		if (controller->ChoiceName.size()==6)
			NewButton(new PosizeEX_Fa6(3,3,-0.68,-0.32,-0.68,-0.32),6);
		
		CB_RememberChoice=new CheckBox<FileOperationPanel*>(0,Border,new PosizeEX_Fa6(3,1,30,20,20,15),0,
			[](FileOperationPanel *&This,bool on)
			{
				This->controller->RememberChoice=on;
			},this);
		TT_RememberChoiceTip=new TinyText(0,Border,new PosizeEX_Fa6(2,1,60,30,20,15),PUIT("对后续所有同类异常执行相同操作."),-1);
		
		UIState=2;
	}
	
	void SetQuestionController(const SynchronizeFunctionDataT <FileOperationTaskQuestion> &tar)
	{
		if (UIState==2)
			CloseQuestionUI();
		if (controller.Valid())
			controller.Continue(0);
		controller=tar;
	}
	
	void SwitchOffUI(bool DeleteBySelft)
	{
		if (UIState==2)
			CloseQuestionUI();
		if (DeleteBySelft)
			Border->DelayDelete();
		
		BaseLayer=NULL;
		Border=NULL;
		TT_Title=NULL;
		TT_State=NULL;
		TT_Speed=NULL;
		TT_CurrentName=NULL;
		TT_LeftTime=NULL;
		TT_LeftItems=NULL;
		ProBar=NULL;
		Pause=NULL;
		Cancel=NULL;
		
		UIState=0;
	}
	
	void UpdateUIInfo()
	{
		if (UIState==1||UIState==2)
		{
			string titleText,
				   countText=task->CurrentState==1?PUIT("若干"):llTOstr(task->TotalItem),
				   srcText=GetLastAfterBackSlash(task->Src[0])+(task->Src.size()>=2?PUIT("等"+llTOstr(task->Src.size())+"个位置"):""),
				   dstText=GetLastAfterBackSlash(task->Dst);
			switch (task->OperationType)
			{
				case FileOperationType_Copy:	titleText=PUIT("正在将 ")+countText+PUIT(" 个项目从 ")+srcText+PUIT(" 复制到 ")+dstText;	break;
				case FileOperationType_Move:	titleText=PUIT("正在将 ")+countText+PUIT(" 个项目从 ")+srcText+PUIT(" 移动到 ")+dstText;	break;
				case FileOperationType_Recycle:	titleText=PUIT("正在从 ")+srcText+PUIT(" 回收 ")+countText+PUIT(" 个项目");					break;
				case FileOperationType_Delete:	titleText=PUIT("正在从 ")+srcText+PUIT(" 删除 ")+countText+PUIT(" 个项目");					break;
			}
			TT_Title->SetText(titleText);
		}
		if (UIState==1)
		{
			TT_State->SetText(PUIT(task->PauseOrCancel==2?"正在取消...":"已完成 "+llTOstr(task->GetPercent()*100)+"%"));
			TT_Speed->SetText(GetFileSizeString(task->GetSpeed())+"/S");
			TT_CurrentName->SetText(PUIT("名称: ")+task->GetCurrentStepName()+" ("+GetFileSizeString(task->CurrentItemTotalSize-task->CurrentItemSolvedSize)+")");
			TT_LeftTime->SetText(PUIT("剩余时间: ")+llTOstr((task->TotalSize-task->CompletedSize-task->CurrentItemSolvedSize)/max(task->GetGlobalSpeed(),1ull))+PUIT(" 秒"));
			TT_LeftItems->SetText(PUIT("剩余项目: ")+llTOstr(task->TotalItem-task->CompletedItem)+" ("+GetFileSizeString(task->TotalSize-task->CompletedSize-task->CurrentItemSolvedSize)+")");
			ProBar->SetPercent((task->CompletedSize+task->CurrentItemSolvedSize)*1.0/max((unsigned long long)task->TotalSize,1ull));
			Pause->SetButtonText(PUIT(task->PauseOrCancel==0?"暂停":"继续"));
		}
	}
	
	void SwitchOnUI(Widgets *baselayer)
	{
		if (BaseLayer!=NULL)
			SwitchOffUI(1);
		BaseLayer=baselayer;
		
		Border=new BorderRectLayer(0,BaseLayer,new PosizeEX_Fa6_Full);
		TT_Title=new TinyText(0,Border,new PosizeEX_Fa6(2,3,30,30,10,20),"",-1);
		TT_State=new TinyText(0,Border,new PosizeEX_Fa6(2,3,30,30,30,30),"",-1);
		TT_State->SetFontSize(20);
		TT_Speed=new TinyText(0,Border,new PosizeEX_Fa6(1,3,200,30,30,30),"",1);
		TT_CurrentName=new TinyText(0,Border,new PosizeEX_Fa6(2,1,30,30,20,75),"",-1);
		TT_LeftTime=new TinyText(0,Border,new PosizeEX_Fa6(2,1,30,30,20,55),"",-1);
		TT_LeftItems=new TinyText(0,Border,new PosizeEX_Fa6(2,1,30,30,20,35),"",-1);
		ProBar=new ProgressBar(0,Border,new PosizeEX_Fa6(2,2,30,30,65,100));
		Pause=new Button <FileOperationPanel*> (0,ProBar,new PosizeEX_Fa6(2,2,0,0,0,0),"",
			[](FileOperationPanel *&This)
			{
				This->task->Pause(This->task->PauseOrCancel==0);
			},this);
		for (int i=0;i<=2;++i)
			Pause->SetButtonColor(i,{255,255,255,i*100});
		Cancel=new Button <FileOperationPanel*> (0,Border,new PosizeEX_Fa6(1,1,100,30,30,30),PUIT("取消"),
			[](FileOperationPanel *&This)
			{
				This->task->Cancel();
				if (This->controller.Valid())
					This->controller.Continue(0);
			},this);
		
		UIState=1;
		
		UpdateUIInfo();
		if (controller.Valid())
			ShowQuestionUI();
	}
	
	FileOperationPanel(int _index,FileOperationTask *_task):index(_index),task(_task) {}
};

int FileOperationMode=FileOperationMode_TaskParallel;
map <int,Doublet <FileOperationTask*,FileOperationPanel*> > FileOperationWaitingPool,FileOperationRunningPool;
Uint32 TimerID_FileOperationPanel=0,TimerInterval_FileOperationPanelUpdate=200;
bool RingWhenAllFileOperationIsCompleted=0;

inline bool IsFileOperationFree()
{return FileOperationWaitingPool.empty()&&FileOperationRunningPool.empty();}

inline int FileOperationTaskCount()
{return FileOperationWaitingPool.size()+FileOperationRunningPool.size();}

inline bool IsFileOperationPopupPanelOn()
{return MBL_FileOperationPanel!=NULL;}

inline bool IsFileOperationPanelOn()
{return LVT_FileOperationPanels!=NULL;}

void RemoveFileOperationPanel(FileOperationPanel *panel)
{
	panel->SetQuestionController(SynchronizeFunctionDataT <FileOperationTaskQuestion>());
	if (IsFileOperationPanelOn())
	{
		int pos=LVT_FileOperationPanels->Find(panel->index);
		if (pos!=-1)
			LVT_FileOperationPanels->DeleteListContent(pos);
	}
	delete panel;
}

void CloseFileOperationPanel()
{
	if (!IsFileOperationPanelOn()) return;
	for (auto mp:FileOperationRunningPool)
		mp.second.b->SwitchOffUI(0);
	for (auto mp:FileOperationWaitingPool)
		mp.second.b->SwitchOffUI(0);
	
	LVT_FileOperationPanels->GetFa()->DelayDelete();
	LVT_FileOperationPanels=NULL;
}

void ShowFileOperationPanelInLayer(Widgets *lay)
{
	CloseFileOperationPanel();
	if (lay==NULL) return;
	
	LVT_FileOperationPanels=new ListViewTemplate <int> (0,lay,new PosizeEX_Fa6_Full,NULL);
	LVT_FileOperationPanels->SetDefaultRowColor(0,RGBA_TRANSPARENT);
	LVT_FileOperationPanels->SetDefaultRowColor(1,RGBA_GRAY_W8A[0]);
	LVT_FileOperationPanels->SetDefaultRowColor(2,RGBA_GRAY_W8A[0]);
	LVT_FileOperationPanels->SetRowHeightAndInterval(240,5);
	LVT_FileOperationPanels->SetEnablePosEvent(0);
	
	for (auto mp:FileOperationRunningPool)
		mp.second.b->SwitchOnUI(LVT_FileOperationPanels->PushbackContent(mp.first));
	for (auto mp:FileOperationWaitingPool)
		mp.second.b->SwitchOnUI(LVT_FileOperationPanels->PushbackContent(mp.first));
}

void PopupFileOperationPanel()
{
	if (!IsFileOperationFree()&&MBL_FileOperationPanel==NULL)
	{
		MBL_FileOperationPanel=new MessageBoxLayer(0,PUIT("文件操作状态"),444,FileOperationTaskCount()==1?274:274+240+5);
		MBL_FileOperationPanel->SetBackgroundColor(DUC_MessageLayerBackground);
		MBL_FileOperationPanel->SetTopAreaColor(DUC_MessageLayerTopAreaColor);
		MBL_FileOperationPanel->EnableShowTopAreaColor(1);
		MBL_FileOperationPanel->SetClickOutsideReaction(1);
		MBL_FileOperationPanel->SetCloseTriggerFunc([](void*)
		{
			CloseFileOperationPanel();
			MBL_FileOperationPanel=NULL;
		});
		Widgets *lay=new Layer(0,MBL_FileOperationPanel,new PosizeEX_Fa6(2,2,2,2,32,0));
		ShowFileOperationPanelInLayer(lay);
	}
}

void SetFileOperationTaskQuestion(SynchronizeFunctionDataT <FileOperationTaskQuestion> tar)//it would only happen in running tasks
{
	auto mp=FileOperationRunningPool.find(tar->index);
	if (mp==FileOperationRunningPool.end())
		tar.Continue(0);//It should be impossible to reach here.
	else
	{
		if (!IsFileOperationPanelOn())
			PopupFileOperationPanel();
		SetSystemBeep(SetSystemBeep_Notification);
		mp->second.b->SetQuestionController(tar);
		mp->second.b->ShowQuestionUI();
	}
}

void FileOperationTaskAllocator(int type,FileOperationTask *task)//type:1:New 2:End
{
	if (FileOperationQuitFlag) return;
	if (type==1)
	{
//		DD[3]<<"Add"<<endl;
		FileOperationPanel *panel=new FileOperationPanel(task->Index,task);
		FileOperationWaitingPool[task->Index]={task,panel};
		if (ProBar_FileOperationGeneralProgress==NULL)
		{
//			DD[3]<<"ProgressBar created"<<endl;
			ProBar_FileOperationGeneralProgress=new ProgressBar(0,PUI_FA_MAINWINDOW,new PosizeEX_Fa6(1,1,300,50,30,50));//??
			ProBar_FileOperationGeneralProgress->SetBorderColor(ThemeColorM[4]);
			Bu_OpenFileOperationPopupPanel=new ButtonI(0,ProBar_FileOperationGeneralProgress,new PosizeEX_Fa6_Full,"0%",
				[](int&)
				{
					if (!IsFileOperationPopupPanelOn())
						PopupFileOperationPanel();
				},0);
			for (int i=0;i<=2;++i)
				Bu_OpenFileOperationPopupPanel->SetButtonColor(i,{100,100,100,i*50});
//			DD[3]<<"Timer Added"<<endl;
			TimerID_FileOperationPanel=SDL_AddTimer(TimerInterval_FileOperationPanelUpdate,
				[](Uint32 itv,void*)->Uint32
				{
					PUI_SendFunctionEvent([](void*)
					{
						double percentSum=0;
						for (auto mp:FileOperationRunningPool)
							mp.second.b->UpdateUIInfo(),
							percentSum+=mp.second.a->GetPercent();
						for (auto mp:FileOperationWaitingPool)
							mp.second.b->UpdateUIInfo(),
							percentSum+=mp.second.a->GetPercent();
						if (ProBar_FileOperationGeneralProgress!=NULL&&!IsFileOperationFree())
						{
							ProBar_FileOperationGeneralProgress->SetPercent(percentSum/FileOperationTaskCount());
							Bu_OpenFileOperationPopupPanel->SetButtonText(llTOstr(percentSum/FileOperationTaskCount()*100)+"%");
						}
					});
					return itv;
				},NULL);
		}
		if (IsFileOperationPanelOn())
//			DD[3]<<"Switch on panel"<<endl,
			panel->SwitchOnUI(LVT_FileOperationPanels->PushbackContent(task->Index));
		task=NULL;
	}
	else if (type==2)
	{
//		DD[3]<<"End"<<endl;
		SDL_WaitThread(task->CurrentThread,NULL);
		SDL_DestroyMutex(task->Mu);
		RemoveFileOperationPanel(FileOperationRunningPool[task->Index].b);
		FileOperationRunningPool.erase(task->Index);
		if (InThisSet(CurrentPath.type,PFE_PathType_Dir,PFE_PathType_Volume)&&task->Dst==CurrentPath.str||CurrentPath.type==PFE_PathType_RecycleBin)//??
			RefreshCurrentPath();
		DeleteToNULL(task);
		if (IsFileOperationPopupPanelOn())//??
			if (IsFileOperationFree())
			{
				CloseFileOperationPanel();
				DelayDeleteToNULL(MBL_FileOperationPanel);
			}
			else MBL_FileOperationPanel->SetrPS(Posize(0,0,444,FileOperationTaskCount()==1?274:274+240+5)+MBL_FileOperationPanel->GetrPS());
		if (IsFileOperationFree())
		{
//			DD[3]<<"ProgressBar deleted"<<endl;
			DelayDeleteToNULL(ProBar_FileOperationGeneralProgress);
			SDL_RemoveTimer(TimerID_FileOperationPanel);
			TimerID_FileOperationPanel=0;
			if (RingWhenAllFileOperationIsCompleted)//??
				SetSystemBeep(SetSystemBeep_Notification);
		}
	}
	else DD[2]<<"FileOperationTaskAllocator: Unknown type "<<type<<endl;
	
//	DD[3]<<"Allocate Presolve OK"<<endl;
	if (InThisSet(FileOperationMode,FileOperationMode_Queue,FileOperationMode_TaskParallel))
	{
//		DD[3]<<"Allocate"<<endl;
		if (!FileOperationWaitingPool.empty())
		{
//			DD[3]<<"Waiting to Running"<<endl;
			auto mp=FileOperationWaitingPool.begin();
			FileOperationRunningPool[mp->first]=mp->second;
			task=mp->second.a;
			FileOperationWaitingPool.erase(mp);
		}
		if (task!=NULL)
		{
//			DD[3]<<"Run"<<endl;
			task->CurrentState=1;
			if (FileOperationMode==FileOperationMode_Queue&&FileOperationRunningPool.empty())
				ThreadPerformFileOperation(task);
			else if (FileOperationMode==FileOperationMode_TaskParallel)
				ThreadPerformFileOperation(task);
		}
	}
}

int AddFileOperationTask(const vector <string> &src,const string &dst,int operationType,bool popupPanel=1)//return 0 means presolved; dst should be a directory
{
	if (src.empty()) return -1;
//	DD[3]<<"AddFileOperationTask"<<endl;
	static int OperationIDAssign=0;
	FileOperationTask *task=new FileOperationTask(src,dst,operationType,++OperationIDAssign);
	task->Mu=SDL_CreateMutex();
	FileOperationTaskAllocator(1,task);
	if (popupPanel)
		if (IsFileOperationPopupPanelOn())
			MBL_FileOperationPanel->SetrPS(Posize(0,0,444,FileOperationTaskCount()==1?274:274+240+5)+MBL_FileOperationPanel->GetrPS());
		else PopupFileOperationPanel();
//	DD[3]<<"AddTask OK"<<endl;
	return OperationIDAssign;
}

void FileOperationQuit()//if exsit running task,it will force quit! Call IsFileOperationFree first.
{
	DD[4]<<"FileOperationQuit"<<endl;
	FileOperationQuitFlag=1;
	if (TimerID_FileOperationPanel!=0)
	{
		SDL_RemoveTimer(TimerID_FileOperationPanel);
		TimerID_FileOperationPanel=0;
	}
	if (ProBar_FileOperationGeneralProgress!=NULL)
		DelayDeleteToNULL(ProBar_FileOperationGeneralProgress);
	CloseFileOperationPanel();
	if (IsFileOperationPopupPanelOn())
		DelayDeleteToNULL(MBL_FileOperationPanel);
	for (auto mp:FileOperationWaitingPool)
	{
		SDL_DestroyMutex(mp.second.a->Mu),
		RemoveFileOperationPanel(mp.second.b);
		delete mp.second.a;
	}
	for (auto mp:FileOperationRunningPool)
	{
		SDL_WaitThread(mp.second.a->CurrentThread,NULL);
		SDL_DestroyMutex(mp.second.a->Mu);
		RemoveFileOperationPanel(mp.second.b);
		delete mp.second.a;
	}
	FileOperationWaitingPool.clear();
	FileOperationRunningPool.clear();
	DD[5]<<"FileOperationQuit"<<endl;
}
//End of FileOperation Area

//Search Area
vector <PFE_Path> LastSearchResult;//0:Search path self; 1~n:Search result
string LastSearchPatternString;

int SearchResultCountToReject=0;
unsigned int SearchStartTick=0;

set <string> SearchFileContentTarget,
			 SearchFileContentTargetInThread;
atomic_bool SearchTextFileContent(0);
unsigned int UserEvent_SearchFileResult=0;
SDL_Thread *Th_SearchFile=NULL;
const int SearchResultUpdateTickInterval=100;
atomic_bool QuitSearchThreadFlag(0);
atomic_int SearchedBufferCount(0);
void Thread_SearchAllFileR(const string &basepath,const regex &pattern,unsigned int &lastSendTick,vector <PFE_Path> *&buffer)
{
	long long hFiles=0;
	_wfinddatai64_t da;
	if ((hFiles=_wfindfirsti64(Utf8ToUnicode(basepath+"\\*").c_str(),&da))!=-1)
		do
		{
			if (QuitSearchThreadFlag)
				return;
			string str=DeleteEndBlank(UnicodeToUtf8(da.name));
			if (str!="."&&str!=".."&&regex_search(str,pattern))
				buffer->push_back(PFE_Path((da.attrib&_A_SUBDIR)?PFE_PathType_Dir:PFE_PathType_File,basepath+"\\"+str,str));
			else if (SearchTextFileContent)//??
				if (SearchFileContentTargetInThread.find(Atoa(GetAftername(str)))!=SearchFileContentTargetInThread.end())
				{
//					CFileIO io(basepath+"\\"+str,CFileIO::OpenMode_ReadOnly);
//					auto vec=ReadTextLinesFromBasicIO(io);//!! Need improve.
//					for (const auto &vp:vec)
//						if (regex_search(vp,pattern))
//						{
//							buffer->push_back(PFE_Path((da.attrib&_A_SUBDIR)?PFE_PathType_Dir:PFE_PathType_File,basepath+"\\"+str,str));
//							break;
//						}
					ifstream fin(Utf8ToAnsi(basepath+"\\"+str));
					if (fin.is_open())
					{
						string s;
						bool flag=0;
						while (!flag&&getline(fin,s))
							if (regex_search(s,pattern))
								flag=1;
						if (flag)
							buffer->push_back(PFE_Path((da.attrib&_A_SUBDIR)?PFE_PathType_Dir:PFE_PathType_File,basepath+"\\"+str,str));
					}
				}
			if ((da.attrib&_A_SUBDIR)&&str!="."&&str!="..")
				Thread_SearchAllFileR(basepath+"\\"+str,pattern,lastSendTick,buffer);
		}
		while (_wfindnexti64(hFiles,&da)==0);
	_findclose(hFiles);
	unsigned int curtick=SDL_GetTicks();
	if (!buffer->empty()&&(curtick-lastSendTick>SearchResultUpdateTickInterval))
	{
		PUI_SendUserEvent(UserEvent_SearchFileResult,0,buffer);
		buffer=new vector <PFE_Path>();
		SearchedBufferCount++;
		lastSendTick=curtick;
	}
	else if (buffer->empty()&&(curtick-lastSendTick>1000))
	{
		PUI_SendUserEvent(UserEvent_SearchFileResult,2);
		lastSendTick=curtick;
	}
	if (QuitSearchThreadFlag)
		return;
}

void StopSearchThread()//??
{
	DD[4]<<"StopSearchThread"<<endl;
	QuitSearchThreadFlag=1;
	SDL_WaitThread(Th_SearchFile,NULL);
	Th_SearchFile=NULL;
	QuitSearchThreadFlag=0;
	SearchResultCountToReject+=SearchedBufferCount;
	SearchedBufferCount=0;
	DD[5]<<"StopSearchThread"<<endl;
}

void StartSearchThread(const PFE_Path &path,const string &patternStr)
{
	DD[4]<<"StartSearchThread"<<endl;
	regex pattern;
	try 
	{
		pattern=regex(patternStr,regex::optimize|regex::icase|regex::nosubs|regex::ECMAScript);//??
	}
	catch(regex_error)
	{
		WarningMessageButton(SetSystemBeep_Error,PUIT("错误"),PUIT("正则表达式有误！"));
		return;
	}
	DD[0]<<"Start search "<<patternStr<<" in "<<path.str<<endl;
	if (Th_SearchFile!=NULL)
		StopSearchThread();
	LastSearchResult.clear();
	LastSearchResult.push_back(path);
	LastSearchPatternString=patternStr;
	SearchFileContentTargetInThread=SearchFileContentTarget;
	SearchStartTick=SDL_GetTicks();
	Th_SearchFile=SDL_CreateThread([](void *userdata)->int
	{
		auto *data=(Doublet <string,regex>*)userdata;
		SearchedBufferCount=0;//??
		unsigned int lastsendtick=0;
		vector <PFE_Path> *buffer=new vector <PFE_Path> ();
		Thread_SearchAllFileR(data->a,data->b,lastsendtick,buffer);
		if (!buffer->empty())
		{
			PUI_SendUserEvent(UserEvent_SearchFileResult,0,buffer);
			SearchedBufferCount++;
		}
		PUI_SendUserEvent(UserEvent_SearchFileResult,1);
		delete data;
		return 0;
	},"Thread_SearchFile",(void*)(new Doublet<string,regex>(path.str,pattern)));
	DD[5]<<"StartSearchThread"<<endl;
}
//End of Search Area

//Sensize Area
unsigned int UserEvent_Sensize=0,
			 UserEvent_SensizeLoad=0,
			 UserEvent_SensizeSave=0;
class SensizeData//Also function as metadata recorder.
{
	public:
		#define SENSIZEDATA_FILEVERSION 1
		
		struct SensizeNodeDataLight
		{
			string name;
			PAL_TimeP CreateTime,
					  AccessTime,
					  WriteTime;
			unsigned long long size=0;//if file, means file size, if dir, means total file size in this dir.
			unsigned int Attribute=0;
			unsigned int Dep=0;
			unsigned int IsDir=0;
		};
		
		enum
		{
			ERR_None=0,
			ERR_CannotOpenFile,
			ERR_BadVersion,
			ERR_BadCheckCode,
			ERR_BadNodeCheckCode,
			ERR_BadFileNodeDep,
			ERR_FstreamBad,
			ERR_CurrentIsSensizing,
			ERR_CurrentIsLoading,
			ERR_CurrentIsSaving,
			ERR_Canceled,
			ERR_NotSavable,
		};
		
	protected:
		struct SensizeNodeData
		{
			SensizeNodeData *fa=nullptr;
			map <Doublet<bool,string>,SensizeNodeData*> childs;//bool:0 file 1:dir
			string name;
			PAL_TimeP CreateTime,
					  AccessTime,
					  WriteTime;
			unsigned long long size=0;//if file, means file size, if dir, means total file size in this dir.
			unsigned int Attribute=0;
			unsigned int Dep=0;
			unsigned int IsDir=0;

			SensizeNodeDataLight ToLight() const
			{
				SensizeNodeDataLight re;
				re.name=name;
				re.CreateTime=CreateTime;
				re.AccessTime=AccessTime;
				re.WriteTime=WriteTime;
				re.size=size;
				re.Attribute=Attribute;
				re.Dep=Dep;
				re.IsDir=IsDir;
				return re;
			}

			~SensizeNodeData()
			{
				for (auto mp:childs)
					delete mp.second;
			}
		};
		
		SensizeNodeData *BaseNode=nullptr,
						*LastQueryNode=nullptr;
		PFE_Path BasePath;//,LastQueryPath;
		unsigned long long cnt=0;
		SDL_Thread *Th_Sensize=NULL;
		atomic_bool StopSensizeFlag;
		atomic_bool SensizeOK;
		atomic_int CurrentThreadFunction;//0:no thread run 1:sensizing 2:loading 3:saving
		PAL_TimeP LastSensizeTime;
		string LS_Path;//??
		//Need mutex?? Parallel conflict??
		
		unsigned long long Thread_SensizeAllFile(const string &str,SensizeNodeData *p)
		{
			if (StopSensizeFlag)
				return 0;
			wstring wstr=Utf8ToUnicode(str+"\\*");
			long long hFiles=0;
			_wfinddatai64_t da;
			if ((hFiles=_wfindfirsti64(wstr.c_str(),&da))!=-1)
				do
				{
					if (StopSensizeFlag)
						break;
					string name=DeleteEndBlank(UnicodeToUtf8(da.name));
					if (name!="."&&name!="..")
					{
						++cnt;
						SensizeNodeData *q=new SensizeNodeData();
						q->fa=p;
						p->childs[{da.attrib&_A_SUBDIR?1:0,name}]=q;
						q->name=name;
						tm *ct=localtime(&da.time_create),
						   *at=localtime(&da.time_access),
						   *wt=localtime(&da.time_write);
						auto checkandWrite=[](const tm *t)->PAL_TimeP
						{
							if (t==NULL) return DD[2]<<"localtime Error in line "<<__LINE__<<endl,PAL_TimeP();
							else return PAL_TimeP(*t);
						};
						q->CreateTime=checkandWrite(ct);
						q->AccessTime=checkandWrite(at);
						q->WriteTime=checkandWrite(wt);
						q->Attribute=da.attrib;
						q->Dep=p->Dep+1;
						q->IsDir=da.attrib&_A_SUBDIR?1:0;
						if (q->IsDir)
							q->size=Thread_SensizeAllFile(str+"\\"+name,q);
						else q->size=da.size;
						p->size+=q->size;
					}
				}
				while (_wfindnexti64(hFiles,&da)==0);
			_findclose(hFiles);
			return p->size;
		}
		
		int ReadNode(ifstream &fin,unsigned int dep,SensizeNodeData *fa)
		{
			SensizeNodeData *p=new SensizeNodeData();
			p->name=FileReadString(fin);
			ReadDataBin(fin,p->CreateTime);
			ReadDataBin(fin,p->AccessTime);
			ReadDataBin(fin,p->WriteTime);
			ReadDataBin(fin,p->size);
			ReadDataBin(fin,p->Attribute);
			ReadDataBin(fin,p->Dep);
			ReadDataBin(fin,p->IsDir); 
			p->fa=fa;
			fa->childs[{p->IsDir,p->name}]=p;
			if (p->Dep!=dep)
				return ERR_BadFileNodeDep;
			
			unsigned long long childCnt=0;
			ReadDataBin(fin,childCnt);
			int retcode;
			for (unsigned long long i=0;i<childCnt;++i)
				if (retcode=ReadNode(fin,dep+1,p))
					return retcode;
			if (fin.bad()||fin.fail())
				return ERR_FstreamBad;
			return ERR_None;
		}
		
		int WriteNode(ofstream &fout,const SensizeNodeData *p)
		{
			FileWriteString(fout,p->name);
			WriteDataBin(fout,p->CreateTime);
			WriteDataBin(fout,p->AccessTime);
			WriteDataBin(fout,p->WriteTime);
			WriteDataBin(fout,p->size);
			WriteDataBin(fout,p->Attribute);
			WriteDataBin(fout,p->Dep);
			WriteDataBin(fout,p->IsDir);
			
			unsigned long long childSize=p->childs.size();
			WriteDataBin(fout,childSize);
			int retcode;
			for (auto mp:p->childs)
				if (retcode=WriteNode(fout,mp.second))
					return retcode;
			if (fout.bad()||fout.fail())
				return ERR_FstreamBad;
			return ERR_None;
		}
		
		int _LoadFile()
		{
			ifstream fin(Utf8ToAnsi(LS_Path).c_str(),ios::binary);
			if (!fin.is_open())
				return ERR_CannotOpenFile;
			int version,checkcode,ckcd;
			ReadDataBin(fin,version);
			if (version!=SENSIZEDATA_FILEVERSION)
				return ERR_BadVersion;
			ReadDataBin(fin,checkcode);
			PFE_Path path;
			ReadDataBin(fin,path.type);
			path.str=FileReadString(fin);
			path.name=FileReadString(fin);
			ReadDataBin(fin,path.code);
			PAL_TimeP lasttime;
			ReadDataBin(fin,lasttime);
			unsigned long long _cnt=0,basesize=0,baseChildCnt=0;
			ReadDataBin(fin,_cnt);
			ReadDataBin(fin,basesize);
			ReadDataBin(fin,baseChildCnt);
			ReadDataBin(fin,ckcd);
			if (ckcd!=checkcode)
				return ERR_BadCheckCode;

			BasePath=path;
			LastSensizeTime=lasttime;
			cnt=_cnt;
			BaseNode=new SensizeNodeData();
			BaseNode->name=BasePath.str;
			BaseNode->IsDir=1;
			BaseNode->size=basesize;
			
			int errorcode;
			for (unsigned long long i=0;i<baseChildCnt;++i)
				if (errorcode=ReadNode(fin,1,BaseNode))
					return errorcode;
			ReadDataBin(fin,ckcd);
			if (ckcd!=checkcode)
				return ERR_BadCheckCode;
			if (fin.bad()||fin.bad())
				return ERR_FstreamBad;
			return ERR_None;
		}

		int _SaveFile()
		{
			ofstream fout(Utf8ToAnsi(LS_Path).c_str(),ios::binary);
			if (!fout.is_open())
				return ERR_CannotOpenFile;
			int version=SENSIZEDATA_FILEVERSION,checkcode=rand()+100;
			WriteDataBin(fout,version);
			WriteDataBin(fout,checkcode);
			WriteDataBin(fout,BasePath.type);
			FileWriteString(fout,BasePath.str);
			FileWriteString(fout,BasePath.name);
			WriteDataBin(fout,BasePath.code);
			WriteDataBin(fout,LastSensizeTime);
			WriteDataBin(fout,cnt);
			WriteDataBin(fout,BaseNode->size);
			unsigned long long childSize=BaseNode->childs.size();
			WriteDataBin(fout,childSize);
			WriteDataBin(fout,checkcode);

			int errorcode;
			for (auto mp:BaseNode->childs)
				if (errorcode=WriteNode(fout,mp.second))
					return errorcode;
			WriteDataBin(fout,checkcode);
			if (fout.bad()||fout.fail())
				return ERR_FstreamBad;
			return ERR_None;
		}

		static int Thread_Sensize(void *userdata)
		{
			SensizeData *data=(SensizeData*)userdata;
			if (data->CurrentThreadFunction==1)
			{
				data->Thread_SensizeAllFile(data->BasePath.str,data->BaseNode);
				if (!data->StopSensizeFlag)
				{
					PUI_SendUserEvent(UserEvent_Sensize,0,data);
					data->SensizeOK=1;
				}
				else PUI_SendUserEvent(UserEvent_Sensize,ERR_Canceled,data);
			}
			else if (data->CurrentThreadFunction==2)
			{
				int retCode=data->_LoadFile();
				if (data->StopSensizeFlag)
					retCode=ERR_Canceled;
				if (retCode==0)
					data->SensizeOK=1;
				PUI_SendUserEvent(UserEvent_SensizeLoad,retCode,data);
			}
			else if (data->CurrentThreadFunction==3)
			{
				int retCode=data->_SaveFile();
				if (data->StopSensizeFlag)
					retCode=ERR_Canceled;
				data->SensizeOK=1;
				PUI_SendUserEvent(UserEvent_SensizeSave,retCode,data);
			}
			data->Th_Sensize=NULL;
			data->CurrentThreadFunction=0;
			return 0;
		}
		
		SensizeNodeData* FindNode(SensizeNodeData *p,const string &path,size_t pos,bool IsDir)//pos: start pos after basepath
		{
			if (p==BaseNode&&pos==0)
			{
				if (BasePath.str==path&&IsDir) return p;
				for (size_t i=0;i<BasePath.str.length();++i)
					if (path[i]!=BasePath.str[i])
						return nullptr;
				return FindNode(p,path,BasePath.str.length()+1,IsDir);
			}
			if (pos==path.length()+1)
				return p;
			else if (pos>path.length())
				return nullptr;
			size_t pos2=pos;
			while (pos2<path.length()&&path[pos2]!='\\')
				++pos2;
			bool dirFlag=IsDir;
			if (path.substr(pos,path.length()-pos).find('\\')!=path.npos)
				dirFlag=1;
			auto mp=p->childs.find({dirFlag,path.substr(pos,pos2-pos)});
			if (mp!=p->childs.end())
				return FindNode(mp->second,path,pos2+1,IsDir);
			else return nullptr;
		}
		
	public:
		inline bool Find(const PFE_Path &path)
		{
			if (!SensizeOK)
				return 0;
			return (LastQueryNode=FindNode(BaseNode,path.str,0,path.type!=PFE_PathType_File&&path.type!=PFE_PathType_VirtualFile))!=nullptr;
		}
		
		inline SensizeNodeDataLight FindNode(const PFE_Path &path)
		{
			if (Find(path))
				return LastQueryNode->ToLight();
			else return SensizeNodeDataLight();
		}
		
		vector <SensizeNodeDataLight> GetChilds(const PFE_Path &path)
		{
			vector <SensizeNodeDataLight> re;
			if (Find(path))
				for (auto mp:LastQueryNode->childs)
					re.push_back(mp.second->ToLight());
			return re;
		}
		
		inline SensizeNodeDataLight GetLastFoundNode()
		{
			if (!SensizeOK||LastQueryNode==nullptr)
				return SensizeNodeDataLight();
			else return LastQueryNode->ToLight();
		}
		
		inline unsigned long long GetLastFoundSize()
		{
			if (!SensizeOK||LastQueryNode==nullptr)
				return 0;
			else return LastQueryNode->size;
		}
		
		inline double GetLastFoundLocalPercent()
		{
			if (!SensizeOK||LastQueryNode==nullptr)
				return 0;
			else if (LastQueryNode==BaseNode)
				return 1;
			else return (double)LastQueryNode->size/LastQueryNode->fa->size;
		}
		
		inline double GetLastFoundGlobalPercent()
		{
			if (!SensizeOK||LastQueryNode==nullptr)
				return 0;
			else if (LastQueryNode==BaseNode)
				return 1;
			else return (double)LastQueryNode->size/BaseNode->size;
		}
		
		inline unsigned long long GetTotalSize() const
		{
			if (!SensizeOK||BaseNode==nullptr)
				return 0;
			else return BaseNode->size;
		}
		
		inline unsigned long long GetCount() const
		{
			if (SensizeOK) return cnt;
			else return 0;
		}
		
		inline bool IsSensizeOK() const//if OK, then it is ready to get datas from main thread.
		{return SensizeOK;}
		
		inline PAL_TimeP GetLastSensizeTime() const
		{return LastSensizeTime;}
		
		inline PFE_Path GetBasePath() const
		{return BasePath;}
		
		inline string GetLSPath() const
		{return LS_Path;}
		
		void UpdateTarget(const PFE_Path &path,unsigned long long size) const
		{
			if (!SensizeOK) return;
			DD[2]<<"Such function "<<__LINE__<<" is not usable yet!"<<endl;
			//...
		}
		
		void StopSensize()
		{
			if (Th_Sensize==NULL||SensizeOK||CurrentThreadFunction==0) return;
			StopSensizeFlag=1;
			SDL_WaitThread(Th_Sensize,NULL);
			StopSensizeFlag=0;
			CurrentThreadFunction=0;
		}
		
		int SaveFile(const string &savepath)
		{
			if (!SensizeOK||CurrentThreadFunction!=0) return ERR_NotSavable;
			CurrentThreadFunction=3;
			SensizeOK=0;
			LS_Path=savepath;
			Th_Sensize=SDL_CreateThread(Thread_Sensize,"Thread_SaveSensize",this);
			return ERR_None;
		}
		
		int TrySaveFile(const string &savepath)
		{
			if (CurrentThreadFunction==0)
				return SaveFile(savepath);
			else return ERR_CurrentIsSensizing-1+CurrentThreadFunction;
		}
		
		void LoadFile(const string &loadpath)
		{
			Clear();
			DD[0]<<"LoadSensize "<<loadpath<<endl;
			CurrentThreadFunction=2;
			LS_Path=loadpath;
			Th_Sensize=SDL_CreateThread(Thread_Sensize,"Thread_LoadSensize",this);
		}
		
		int TryLoadFile(const string &loadpath)
		{
			if (CurrentThreadFunction==0)
				return LoadFile(loadpath),0;
			else return ERR_CurrentIsSensizing-1+CurrentThreadFunction;
		}
		
		void Clear()
		{
			StopSensize();
			DeleteToNULL(BaseNode);
			BasePath=PFE_Path();
			cnt=0;
			Th_Sensize=NULL;
			StopSensizeFlag=0;
			SensizeOK=0;
			LastQueryNode=nullptr;
		}
		
		void StartSensize(const PFE_Path &basepath)
		{
			if (basepath.type!=PFE_PathType_Dir&&basepath.type!=PFE_PathType_Volume)
				return;
			Clear();
			DD[0]<<"Sensize "<<basepath.str<<endl;
			BasePath=basepath;
			LastSensizeTime=PAL_TimeP::CurrentDateTime();
			BaseNode=new SensizeNodeData();
			BaseNode->name=BasePath.str;
			BaseNode->IsDir=1;
			cnt=1;
			CurrentThreadFunction=1;
			Th_Sensize=SDL_CreateThread(Thread_Sensize,"Thread_StartSensize",this);
		}
		
		int TrySensize(const PFE_Path &basepath)
		{
			if (CurrentThreadFunction==0)
				StartSensize(basepath);
			else return ERR_CurrentIsSensizing-1+CurrentThreadFunction;
		}
		
		~SensizeData()
		{
			Clear();
		}
		
		SensizeData()
		{
			StopSensizeFlag=0;
			SensizeOK=0;
			CurrentThreadFunction=0;
		}
		#undef SENSIZEDATA_FILEVERSION
};
SensizeData CurrentSensize,LoadedSensize;
struct SensizePanelWidgets
{
	MessageBoxLayer *MBL_SensizePanel=NULL;
	Button <int> *Bu_StartSensize=NULL,
				 *Bu_SensizeLoadFrom=NULL,
				 *Bu_SensizeSaveTo=NULL,
				 *Bu_SensizeSave=NULL,
				 *Bu_GogoCurrent=NULL,
				 *Bu_GogoLoaded=NULL,
				 *Bu_Analyze=NULL;
	SingleChoiceButton <int> *SCB_ShowMode=NULL;
	TinyText *TT_SelectShowMode=NULL,
			 *TT_SavedList=NULL;
	SimpleListView <string> *SLV_SavedList=NULL;
	BorderRectLayer *Lay_SLVBorder=NULL;
}SensizePanel;
int ShowSensizeResultMode=1;//0:None 1:Result 2:Virtual 3:Compare

MessageBoxLayer* ShowVirtualFileProperty(const PFE_Path &path)
{
	if (!LoadedSensize.Find(path))
		return WarningMessageButton(SetSystemBeep_Error,PUIT("错误！"),PUIT("无法获取虚拟文件属性！")),nullptr;
	else
	{
		SensizeData::SensizeNodeDataLight data=LoadedSensize.GetLastFoundNode();
		auto mbl=new MessageBoxLayer(0,PUIT("虚拟文件属性"),500,350);
		mbl->EnableShowTopAreaColor(1);
		mbl->SetClickOutsideReaction(1);
		mbl->SetBackgroundColor(DUC_MessageLayerBackground);
		mbl->SetTopAreaColor(DUC_MessageLayerTopAreaColor);
		auto timeToString=[](const PAL_TimeP &t)->string{return PUIT(llTOstr(t.year)+"."+llTOstr(t.month)+"."+llTOstr(t.day)+"-"+llTOstr(t.hour)+":"+llTOstr(t.minute,2)+":"+llTOstr(t.second,2));};
		new TinyText(0,mbl,new PosizeEX_Fa6(3,3,30,100,50,30),PUIT("名称:"),1);
		new TinyText(0,mbl,new PosizeEX_Fa6(2,3,150,30,50,30),path.name,-1);
		new TinyText(0,mbl,new PosizeEX_Fa6(3,3,30,100,90,30),PUIT("类型:"),1);
		new TinyText(0,mbl,new PosizeEX_Fa6(2,3,150,30,90,30),path.type==PFE_PathType_VirtualDir?PUIT("文件夹"):GetAftername(path.name),-1);
		new TinyText(0,mbl,new PosizeEX_Fa6(3,3,30,100,130,30),PUIT("路径:"),1);
		new TinyText(0,mbl,new PosizeEX_Fa6(2,3,150,30,130,30),path.str,-1);
		new TinyText(0,mbl,new PosizeEX_Fa6(3,3,30,100,170,30),PUIT("大小:"),1);
		new TinyText(0,mbl,new PosizeEX_Fa6(2,3,150,30,170,30),GetFileSizeString(data.size)+" | "+llTOstr(data.size)+PUIT("字节"),-1);
		new TinyText(0,mbl,new PosizeEX_Fa6(3,3,30,100,210,30),PUIT("创建时间:"),1);
		new TinyText(0,mbl,new PosizeEX_Fa6(2,3,150,30,210,30),timeToString(data.CreateTime),-1);
		new TinyText(0,mbl,new PosizeEX_Fa6(3,3,30,100,250,30),PUIT("访问时间:"),1);
		new TinyText(0,mbl,new PosizeEX_Fa6(2,3,150,30,250,30),timeToString(data.AccessTime),-1);
		new TinyText(0,mbl,new PosizeEX_Fa6(3,3,30,100,290,30),PUIT("修改时间:"),1);
		new TinyText(0,mbl,new PosizeEX_Fa6(2,3,150,30,290,30),timeToString(data.WriteTime),-1);
		return mbl;
	}
}

void SensizePanelFunc_Load(const string &loadpath)
{
	TT_SensizeState->SetText(PUIT("当前状态: 载入中"));
	TT_SensizeState->SetTextColor({132,232,80,255});
	LoadedSensize.LoadFile(loadpath);//??
	if (InRange(CurrentMainView,CurrentMainView_BVT_0,CurrentMainView_BVT_10)||CurrentMainView==CurrentMainView_LVT_Normal)//??
		RefreshCurrentPath();
}

void SensizeButtonFunc(int&)
{
	if (SensizePanel.MBL_SensizePanel==NULL)
	{
		auto mbl=SensizePanel.MBL_SensizePanel=new MessageBoxLayer(0,PUIT("空间占用分析器"),460,330);
		SensizePanel.MBL_SensizePanel->SetBackgroundColor(DUC_MessageLayerBackground);
		SensizePanel.MBL_SensizePanel->SetTopAreaColor(DUC_MessageLayerTopAreaColor);
		SensizePanel.MBL_SensizePanel->EnableShowTopAreaColor(1);
		SensizePanel.MBL_SensizePanel->SetCloseTriggerFunc([](void*){SensizePanel=SensizePanelWidgets();});
		SensizePanel.Bu_StartSensize=new Button <int> (0,mbl,{30,40,100,100},PUIT("进行分析"),
			[](int&)
			{
				if (CurrentPath.type==PFE_PathType_Dir||CurrentPath.type==PFE_PathType_Volume)
				{
					TT_SensizeState->SetText(PUIT("当前状态: 进行中"));
					TT_SensizeState->SetTextColor({255,138,0,255});
					CurrentSensize.StartSensize(CurrentPath);
					if (InRange(CurrentMainView,CurrentMainView_BVT_0,CurrentMainView_BVT_10)||CurrentMainView==CurrentMainView_LVT_Normal)//??
						RefreshCurrentPath();
				}
				else WarningMessageButton(SetSystemBeep_Error,PUIT("错误!"),PUIT("当前目录不可分析!"));
			},0);
		SetButtonDUCColor(SensizePanel.Bu_StartSensize);
		SensizePanel.Bu_SensizeSave=new Button <int> (0,mbl,{150,40,60,30},PUIT("保存"),
			[](int&)
			{
				auto mbl=new MessageBoxLayer(0,PUIT("输入文件名(覆盖保存):"),400,80);
				mbl->EnableShowTopAreaColor(1);
				mbl->SetBackgroundColor(DUC_MessageLayerBackground);
				mbl->SetTopAreaColor(DUC_MessageLayerTopAreaColor);
				auto tel=new TextEditLine <MessageBoxLayer*> (0,mbl,new PosizeEX_Fa6(2,2,20,85,40,10),
					[](MessageBoxLayer *&mbl,const stringUTF8 &strutf8,TextEditLine <MessageBoxLayer*> *tel,bool isenter)->void
					{
						if (isenter)
						{
							if (!IsFileExsit(UserDataPath+"\\SensizeResults",1))
								CreateDirectoryR(UserDataPath+"\\SensizeResults");
							string name=strutf8.cppString()+".pfesr",savepath=UserDataPath+"\\SensizeResults\\"+name;
							if (name.empty())
								mbl->Close();
							bool invalidflag=0;
							for (int i=0;i<name.length()&&!invalidflag;++i)
								for (int j=0;j<9&&!invalidflag;++j)
									if (name[i]==InvalidFileName[j])
										invalidflag=1;
							if (invalidflag)
								WarningMessageButton(SetSystemBeep_Error,PUIT("错误！"),PUIT("文件名不能包含下列字符: /  \\  :  *  ?  \"  <  >  |"));
							else
							{
								int retcode;
								if (retcode=CurrentSensize.TrySaveFile(savepath))
									if (InThisSet(retcode,SensizeData::ERR_CurrentIsSensizing,SensizeData::ERR_CurrentIsLoading,SensizeData::ERR_CurrentIsSaving))
										WarningMessageButton(SetSystemBeep_Error,PUIT("错误!"),PUIT("当前状态无法保存!"));
									else WarningMessageButton(SetSystemBeep_Error,PUIT("错误!"),PUIT("保存失败! 错误码: "+llTOstr(retcode)));
								else 
								{
									TT_SensizeState->SetText(PUIT("当前状态: 保存中"));
									TT_SensizeState->SetTextColor({100,200,255,255});
									mbl->Close();
								}
							}
						}
					},mbl);
				new Button <TextEditLine <MessageBoxLayer*>* > (0,mbl,new PosizeEX_Fa6(1,2,60,20,40,10),PUIT("保存"),[](TextEditLine <MessageBoxLayer*> *&tel)->void{tel->TriggerFunc();},tel);
			},0);
		SetButtonDUCColor(SensizePanel.Bu_SensizeSave);
		SensizePanel.Bu_SensizeLoadFrom=new Button <int> (0,mbl,{150,75,60,30},PUIT("导入"),
			[](int&)
			{
				WarningMessageButton(SetSystemBeep_Error,PUIT("错误!"),PUIT("这个功能还没写...＞﹏＜"));
				//...
			},0);
		SetButtonDUCColor(SensizePanel.Bu_SensizeLoadFrom);
		SensizePanel.Bu_SensizeSaveTo=new Button <int> (0,mbl,{150,110,60,30},PUIT("导出"),
			[](int&)
			{
				WarningMessageButton(SetSystemBeep_Error,PUIT("错误!"),PUIT("这个功能还没写...＞﹏＜"));
				//...
			},0);
		SetButtonDUCColor(SensizePanel.Bu_SensizeSaveTo);
		SensizePanel.TT_SelectShowMode=new TinyText(0,mbl,{28,150,180,30},PUIT("显示模式: "),-1);
		SensizePanel.SCB_ShowMode=new SingleChoiceButton <int> (0,mbl,{30,180,180,120},
			[](int&,const string&,int pos)->void
			{
				if (pos==ShowSensizeResultMode) return;
				ShowSensizeResultMode=pos;
				if (InRange(CurrentMainView,CurrentMainView_BVT_0,CurrentMainView_BVT_10)||CurrentMainView==CurrentMainView_LVT_Normal)//??
					RefreshCurrentPath();
			},0);
		SensizePanel.SCB_ShowMode->SetButtonColor(0,DUC_ClearThemeColor[0]);
		SensizePanel.SCB_ShowMode->SetButtonColor(0,DUC_ClearThemeColor[1]);
		SensizePanel.SCB_ShowMode->SetAccentData(30,15,18);
		SensizePanel.SCB_ShowMode->AddChoice(PUIT("关闭"));
		SensizePanel.SCB_ShowMode->AddChoice(PUIT("正常模式"));
		SensizePanel.SCB_ShowMode->AddChoice(PUIT("虚拟模式"));
		SensizePanel.SCB_ShowMode->AddChoice(PUIT("对比模式"));
		SensizePanel.SCB_ShowMode->ChooseChoice(ShowSensizeResultMode,0);
		SensizePanel.Bu_GogoCurrent=new Button <int> (0,mbl,new PosizeEX_Fa6(2,3,230,30,40,30),PUIT("跳转到本次分析位置"),
			[](int&)
			{
				if (InThisSet(ShowSensizeResultMode,1,3))
					if (CurrentSensize.GetBasePath()==PFE_Path()) WarningMessageButton(SetSystemBeep_Error,PUIT("错误!"),PUIT("路径不存在!"));
					else SetCurrentPath(CurrentSensize.GetBasePath(),SetCurrentPathFrom_User);
				else if (ShowSensizeResultMode==2)
					if (CurrentSensize.GetBasePath()==PFE_Path()) WarningMessageButton(SetSystemBeep_Error,PUIT("错误!"),PUIT("路径不存在!"));
					else SetCurrentPath(LoadedSensize.GetBasePath(),SetCurrentPathFrom_User);
				SensizePanel.MBL_SensizePanel->Close();
				SensizePanel=SensizePanelWidgets();
			},0);
		SetButtonDUCColor(SensizePanel.Bu_GogoCurrent);
		SensizePanel.Bu_GogoLoaded=new Button <int> (0,mbl,new PosizeEX_Fa6(2,3,230,30,75,30),PUIT("跳转到虚拟记录位置"),
			[](int&)
			{
				if (InThisSet(ShowSensizeResultMode,2,3))
					if (LoadedSensize.GetBasePath()==PFE_Path()||!LoadedSensize.IsSensizeOK()) WarningMessageButton(SetSystemBeep_Error,PUIT("错误!"),PUIT("路径不存在!"));
					else SetCurrentPath(PFE_Path(ShowSensizeResultMode==3?PFE_PathType_Dir:PFE_PathType_VirtualDir,LoadedSensize.GetBasePath().str,LoadedSensize.GetBasePath().name,LoadedSensize.GetBasePath().code),SetCurrentPathFrom_User);
				else WarningMessageButton(SetSystemBeep_Error,PUIT("错误!"),PUIT("当前不处于虚拟模式或对比模式!"));
				SensizePanel.MBL_SensizePanel->Close();
				SensizePanel=SensizePanelWidgets();
			},0);
		SetButtonDUCColor(SensizePanel.Bu_GogoLoaded);
		SensizePanel.Lay_SLVBorder=new BorderRectLayer(0,mbl,new PosizeEX_Fa6(2,2,230,30,110,30));
		SensizePanel.TT_SavedList=new TinyText(0,SensizePanel.Lay_SLVBorder,new PosizeEX_Fa6(2,3,20,0,0,30),PUIT("已保存分析: "),-1);
		SensizePanel.SLV_SavedList=new SimpleListView <string> (0,SensizePanel.Lay_SLVBorder,new PosizeEX_Fa6(2,2,0,0,30,0),
			[](string &path,int pos,int click)
			{
				if (pos==-1) return;
				if (click==2) SensizePanelFunc_Load(path);
				else if (click==3)
				{
					vector <MenuData<string> > vec;
					vec.push_back(MenuData<string>(PUIT("载入"),[](string &path){SensizePanelFunc_Load(path);},path));
					vec.push_back(MenuData<string>(PUIT("删除"),[](string &path)
					{
						unsigned int ret=DeleteFileW(DeleteEndBlank(Utf8ToUnicode(path)).c_str());
						if (ret==0)
							WarningMessageButton(SetSystemBeep_Error,PUIT("错误!"),PUIT("无法删除该文件! 错误码: "+llTOstr(GetLastError())));
						else SensizePanel.SLV_SavedList->DeleteListContent(SensizePanel.SLV_SavedList->Find(path));
					},path));
					new Menu1<string>(0,vec);
				}
			});
		for (int i=0;i<=2;++i)
			SensizePanel.SLV_SavedList->SetRowColor(i,DUC_ClearThemeColor[i*2]);
		auto vec=GetAllFile_UTF8(UserDataPath+"\\SensizeResults",1);
		for (auto vp:*vec)
			if (Atoa(GetAftername(vp))==".pfesr")
				SensizePanel.SLV_SavedList->PushbackContent(GetWithOutAftername(vp),UserDataPath+"\\SensizeResults\\"+vp);
		delete vec;
	}
	else SensizePanel.MBL_SensizePanel->Close(),SensizePanel=SensizePanelWidgets();
}
//End of Sensize Area

//MainViewOperation Area
inline int MainView_GetItemCnt()
{
	if (InRange(CurrentMainView,CurrentMainView_BVT_0,CurrentMainView_BVT_10)||InThisSet(CurrentMainView,CurrentMainView_BVT_RecycleBin))
		return BVT_MainView->GetBlockCnt();
	else if (InThisSet(CurrentMainView,CurrentMainView_LVT_Normal,CurrentMainView_LVT_SearchResult))
		return LVT_MainView->GetListCnt();
	else return 0;//??
}

inline int MainView_GetCurrentSelectItem()
{
	int pos=-1;
	if (InRange(CurrentMainView,CurrentMainView_BVT_0,CurrentMainView_BVT_10)||InThisSet(CurrentMainView,CurrentMainView_BVT_RecycleBin))
		pos=BVT_MainView->GetCurrentSelectBlock();
	else if (InThisSet(CurrentMainView,CurrentMainView_LVT_Normal,CurrentMainView_LVT_SearchResult))
		pos=LVT_MainView->GetCurrentSelectLine();
	return pos;
}

inline void MainView_SetSelectItem(int pos)
{
	if (InRange(CurrentMainView,CurrentMainView_BVT_0,CurrentMainView_BVT_10)||InThisSet(CurrentMainView,CurrentMainView_BVT_RecycleBin))
		BVT_MainView->SetSelectBlock(pos);
	else if (InThisSet(CurrentMainView,CurrentMainView_LVT_Normal,CurrentMainView_LVT_SearchResult))
		LVT_MainView->SetSelectLine(pos);
}

inline int MainView_Find(const PFE_Path &path)
{
	int pos=-1;
	if (InRange(CurrentMainView,CurrentMainView_BVT_0,CurrentMainView_BVT_10)||InThisSet(CurrentMainView,CurrentMainView_BVT_RecycleBin))
		pos=BVT_MainView->Find(MainViewData(path));
	else if (InThisSet(CurrentMainView,CurrentMainView_LVT_Normal,CurrentMainView_LVT_SearchResult))
		pos=LVT_MainView->Find(MainViewData(path));
	return pos;
}

inline MainViewData& MainView_GetFuncData(int p)
{
	if (InRange(CurrentMainView,CurrentMainView_BVT_0,CurrentMainView_BVT_10)||InThisSet(CurrentMainView,CurrentMainView_BVT_RecycleBin))
		return BVT_MainView->GetFuncData(p);
	else if (InThisSet(CurrentMainView,CurrentMainView_LVT_Normal,CurrentMainView_LVT_SearchResult))
		return LVT_MainView->GetFuncData(p);
	else return DD[2]<<"GetData from invalid CurrentMainView "<<CurrentMainView<<endl,MainViewData_None;
}

inline PFE_Path& MainView_GetCurrentSelectPath()//Slow??
{return MainView_GetFuncData(MainView_GetCurrentSelectItem()).path;}

inline void MainView_ClearContent();

inline void MainView_SetBackgroundData(const PFE_Path &path)
{
	if (InRange(CurrentMainView,CurrentMainView_BVT_0,CurrentMainView_BVT_10)||InThisSet(CurrentMainView,CurrentMainView_BVT_RecycleBin))
		BVT_MainView->SetBackgroundFuncData(path);
	else if (InThisSet(CurrentMainView,CurrentMainView_LVT_Normal,CurrentMainView_LVT_SearchResult))
		LVT_MainView->SetBackgroundFuncData(path);
}

inline double MainView_GetScrollBarPercent()
{
	if (InRange(CurrentMainView,CurrentMainView_BVT_0,CurrentMainView_BVT_10)||InThisSet(CurrentMainView,CurrentMainView_BVT_RecycleBin))
		return BVT_MainView->GetLargeLayer()->GetScrollBarPercentY();
	else if (InThisSet(CurrentMainView,CurrentMainView_LVT_Normal,CurrentMainView_LVT_SearchResult))
		return LVT_MainView->GetLargeLayer()->GetScrollBarPercentY();
	else return 0;
}

inline void MainView_SetScrollBarPercent(double per)
{
	if (InRange(CurrentMainView,CurrentMainView_BVT_0,CurrentMainView_BVT_10)||InThisSet(CurrentMainView,CurrentMainView_BVT_RecycleBin))
		BVT_MainView->GetLargeLayer()->SetViewPort(4,per);
	else if (InThisSet(CurrentMainView,CurrentMainView_LVT_Normal,CurrentMainView_LVT_SearchResult))
		LVT_MainView->GetLargeLayer()->SetViewPort(4,per);
}

inline void MainView_UpdateIcon(int pos,const SharedTexturePtr &pic,int priority)
{
	if (pos==-1)
		return;
	MainViewData &mvd=MainView_GetFuncData(pos);
	if (mvd.PB_Pic!=nullptr&&mvd.PB_Pic->GetFuncData().c<priority)
		mvd.PB_Pic->GetFuncData().c=priority,
		mvd.PB_Pic->SetPicture(pic,11);
}

inline void MainView_UpdateIcon(const PFE_Path &path,const SharedTexturePtr &pic,int priority)
{
	if (!pic) return;
	MainView_UpdateIcon(MainView_Find(path),pic,priority);
}
//End of MainViewOperation

//MultiSelect Area
bool DisableSingleChooseBVTCheckBoxShow=1,
	 EnableBorderCheckBoxEventThrough=1;

set <PFE_Path> SelectedMainViewData;
map <string,int> SelectedFileTypeCount;
unsigned SelectedFileTypeMask=0;
enum
{
	FileTypeMask_File=1,
	FileTypeMask_Dir=2,
	FileTypeMask_Library=4,
	FileTypeMask_VirtualFile=8,
	FileTypeMask_VirtualDir=16,
	FileTypeMask_Volume=32,
	FileTypeMask_Unknown=64
};

inline bool IsPureDirFile(unsigned masks)
{return (masks&(FileTypeMask_File|FileTypeMask_Dir))==masks;}

inline bool IsPureDirFileVol(unsigned masks)
{return (masks&(FileTypeMask_File|FileTypeMask_Dir|FileTypeMask_Volume))==masks;}

unsigned GetFileTypeMask(const vector <PFE_Path> &vec)
{
	unsigned re=0;
	for (const auto &vp:vec)
		switch (vp.type)
		{
			case PFE_PathType_File:			re|=FileTypeMask_File;		break;
			case PFE_PathType_Dir:			re|=FileTypeMask_Dir;			break;
			case PFE_PathType_Library:		re|=FileTypeMask_Library;		break;
			case PFE_PathType_VirtualFile:	re|=FileTypeMask_VirtualFile;	break;
			case PFE_PathType_VirtualDir:	re|=FileTypeMask_VirtualDir;	break;
			case PFE_PathType_Volume:		re|=FileTypeMask_Volume;		break;
			default:						re|=FileTypeMask_Unknown;		break;
		}
	return re;
}

Doublet <unsigned,set <string> > GetFileTypeCount(const vector <PFE_Path> &vec)
{
	decltype(GetFileTypeCount(vec)) re;
	re.a=0;
	for (const auto &vp:vec)
		switch (vp.type)
		{
			case PFE_PathType_File:
				re.b.insert(Atoa(GetAftername(vp.str)));
				re.a|=FileTypeMask_File;
				break;
			case PFE_PathType_Dir:			re.a|=FileTypeMask_Dir;			break;
			case PFE_PathType_Library:		re.a|=FileTypeMask_Library;		break;
			case PFE_PathType_VirtualFile:	re.a|=FileTypeMask_VirtualFile;	break;
			case PFE_PathType_VirtualDir:	re.a|=FileTypeMask_VirtualDir;	break;
			case PFE_PathType_Volume:		re.a|=FileTypeMask_Volume;		break;
			default:						re.a|=FileTypeMask_Unknown;		break;
		}
	return re;
}

inline void AddSelectedItem(const PFE_Path &path)
{
	if (SelectedMainViewData.insert(path).second)
		switch (path.type)
		{
			case PFE_PathType_File:			++SelectedFileTypeCount[Atoa(GetAftername(path.str))];	SelectedFileTypeMask|=FileTypeMask_File;		break;
			case PFE_PathType_Dir:			++SelectedFileTypeCount["Dir"];							SelectedFileTypeMask|=FileTypeMask_Dir;			break;
			case PFE_PathType_Library:		++SelectedFileTypeCount["Lib"];							SelectedFileTypeMask|=FileTypeMask_Library;		break;
			case PFE_PathType_VirtualFile:	++SelectedFileTypeCount["VFile"];						SelectedFileTypeMask|=FileTypeMask_VirtualFile;	break;
			case PFE_PathType_VirtualDir:	++SelectedFileTypeCount["VDir"];						SelectedFileTypeMask|=FileTypeMask_VirtualDir;	break;
			case PFE_PathType_Volume:		++SelectedFileTypeCount["Vol"]; 						SelectedFileTypeMask|=FileTypeMask_Volume;		break;
			default:						++SelectedFileTypeCount["Unk"];							SelectedFileTypeMask|=FileTypeMask_Unknown;		break;
		}
//	DD[3]<<"Current global FileTypeMask "<<SelectedFileTypeMask<<endl;
}

inline void RemoveSelectedItem(const PFE_Path &path)
{
	if (SelectedMainViewData.erase(path)>0)
		switch (path.type)
		{
			#define MACDU(s,mask)							\
				{											\
					auto mp=SelectedFileTypeCount.find(s);	\
					if (mp!=SelectedFileTypeCount.end())	\
						if (--mp->second<=0)				\
						{									\
							SelectedFileTypeCount.erase(mp);\
							SelectedFileTypeMask&=~mask;	\
						}									\
				}
			case PFE_PathType_File:			MACDU(Atoa(GetAftername(path.str)),FileTypeMask_File);	break;
			case PFE_PathType_Dir:			MACDU("Dir",FileTypeMask_Dir);							break;
			case PFE_PathType_Library:		MACDU("Lib",FileTypeMask_Library);						break;
			case PFE_PathType_VirtualFile:	MACDU("VFile",FileTypeMask_VirtualFile);				break;
			case PFE_PathType_VirtualDir:	MACDU("VDir",FileTypeMask_VirtualDir);					break;
			case PFE_PathType_Volume:		MACDU("Vol",FileTypeMask_Volume);						break;
			default:						MACDU("Unk",FileTypeMask_Unknown);						break;
			#undef MACDU
		}
//	DD[3]<<"Current global FileTypeMask "<<SelectedFileTypeMask<<endl;
}

inline void ClearSelectedItem()
{
	SelectedMainViewData.clear();
	SelectedFileTypeCount.clear();
	SelectedFileTypeMask=0;
}

inline bool IsMainViewSingleSelected()
{return SelectedMainViewData.size()==1;}

inline bool IsMainViewMultiSelected()
{return SelectedMainViewData.size()>=2;}

inline int MainViewItemSelectCount()
{return SelectedMainViewData.size();}

inline PFE_Path GetFirstSelectedItem()
{return *SelectedMainViewData.begin();}

inline void RestoreRefreshSelect()
{
	set <PFE_Path> se=move(SelectedMainViewData);
	ClearSelectedItem();
	int cnt=MainView_GetItemCnt();
	for (int i=0;i<cnt;++i)
	{
		MainViewData &mvd=MainView_GetFuncData(i);
		if (mvd.CB_Choose==nullptr) continue;
		if (se.find(mvd.path)!=se.end())
			mvd.CB_Choose->SetOnOff(1,0),
			AddSelectedItem(mvd.path);
		else mvd.CB_Choose->SetOnOff(0,0);
	}
}

void SelectMainViewRangeEX(Range rg,int mode)//mode0:clear 0 mode1:all set 1 mode2:reverse otherMode:DoNothing
{
	rg=rg&Range(0,(int)MainView_GetItemCnt()-1);
	for (int i=rg.l;i<=rg.r;++i)
	{
		MainViewData &mvd=MainView_GetFuncData(i);
		if (mvd.CB_Choose==NULL) continue;
		int b=mvd.CB_Choose->GetOnOff(),c;
		if (mode==2)
			c=!b;
		else c=mode;
		mvd.CB_Choose->SetOnOff(c,0);
		if (c) AddSelectedItem(mvd.path);
		else RemoveSelectedItem(mvd.path);
	}
	SetNeedUpdateOperationButton();//??
}

inline void SetSelectMainViewItem(int pos,int mode)
{
	if (pos!=-1)
		SelectMainViewRangeEX({pos,pos},mode);
}

inline void SetSelectMainViewItem(const PFE_Path &path,int mode)
{SetSelectMainViewItem(MainView_Find(path),mode);}

inline void SelectMainViewItemRange(const Range &rg,bool selectOn)
{SelectMainViewRangeEX(rg,selectOn?1:0);}

inline void ReverseSelectMainViewItemRange(const Range &rg)
{SelectMainViewRangeEX(rg,2);}

inline void SelectAllMainViewItem(bool selectOn)
{SelectMainViewRangeEX(Range(0,1e9),selectOn?1:0);}

inline void ReverseSelectAllItem()
{SelectMainViewRangeEX(Range(0,1e9),2);}

Widgets::WidgetType WidgetType_BorderCheckBox=Widgets::GetNewWidgetTypeID("BorderCheckBox");
template <class T> class BorderCheckBox:public CheckBox <T>
{
	protected:
		unsigned LastSwitchEventTimeStamp=0;//??
		
		virtual void CheckPos(const PUI_PosEvent *event,int mode)
		{
			if (mode&this->PosMode_LoseFocus)
			{
				if (this->stat!=this->Stat_NoFocus)
					if (!this->Win->IsPosFocused()||!this->InButton(event->pos)||NotInSet(this->Win->GetOccupyPosWg(),this,nullptr)||mode&this->PosMode_ForceLoseFocus)
					{
						this->stat=this->Stat_NoFocus;
						this->RemoveNeedLoseFocus();
						this->Win->SetPresentArea(this->CoverLmt);
					}
			}
			else if ((mode&this->PosMode_Default)&&this->InButton(event->pos))
				switch (event->posType)
				{
					case PUI_PosEvent::Pos_Down:
						if (event->button==PUI_PosEvent::Button_MainClick||this->ThisButtonSolveSubClick&&event->button==PUI_PosEvent::Button_SubClick)
						{
							this->stat=this->Stat_Down;
							this->SetNeedLoseFocus();
							if (!EnableBorderCheckBoxEventThrough)
								this->Win->StopSolvePosEvent();
							this->Win->SetPresentArea(this->CoverLmt);
						}
						break;
					case PUI_PosEvent::Pos_Up:
						if (this->stat==this->Stat_Down)
						{
							LastSwitchEventTimeStamp=event->timeStamp;
							this->TriggerButtonFunction(this->ThisButtonSolveSubClick&&event->button==PUI_PosEvent::Button_SubClick);
							if (event->type==event->Event_TouchEvent)
							{
								this->stat=this->Stat_NoFocus;
								this->RemoveNeedLoseFocus();
							}
							else this->stat=this->Stat_Focus;
							if (!EnableBorderCheckBoxEventThrough)
								this->Win->StopSolvePosEvent();
							this->Win->SetPresentArea(this->CoverLmt);
						}
						break;
					case PUI_PosEvent::Pos_Motion:
						if (this->stat==this->Stat_NoFocus)
						{
							this->stat=this->Stat_Focus;
							this->SetNeedLoseFocus();
							this->Win->SetPresentArea(this->CoverLmt);
						}
//						Win->StopSolvePosEvent();
						break;
				}
		}
		
		virtual void Show(Posize &lmt)
		{
			PUI_Window *Win=this->Win;
			Posize gPS=this->gPS;
			
			if (!DisableSingleChooseBVTCheckBoxShow||MainViewItemSelectCount()>=1)
				for (int i=0;i<=1;++i)
					if (this->on||this->stat!=this->Stat_NoFocus)
						Win->RenderDrawRectWithLimit(gPS.Shrink(i),ThemeColor(this->BorderColor[this->stat]),lmt);
			if (this->on&&(!DisableSingleChooseBVTCheckBoxShow||IsMainViewMultiSelected()))
			{
				Posize ps=gPS.Shrink(3);
				RGBA ChooseColor=this->ChooseColor;
				Win->RenderFillRect(lmt&Posize(ps.x,ps.y,13,3),ThemeColor(ChooseColor));
				Win->RenderFillRect(lmt&Posize(ps.x,ps.y+3,3,10),ThemeColor(ChooseColor));
				Win->RenderFillRect(lmt&Posize(ps.x2()-13,ps.y,13,3),ThemeColor(ChooseColor));
				Win->RenderFillRect(lmt&Posize(ps.x2()-3,ps.y+3,3,10),ThemeColor(ChooseColor));
				Win->RenderFillRect(lmt&Posize(ps.x,ps.y2()-3,13,3),ThemeColor(ChooseColor));
				Win->RenderFillRect(lmt&Posize(ps.x,ps.y2()-13,3,10),ThemeColor(ChooseColor));
				Win->RenderFillRect(lmt&Posize(ps.x2()-13,ps.y2()-3,13,3),ThemeColor(ChooseColor));
				Win->RenderFillRect(lmt&Posize(ps.x2()-3,ps.y2()-13,3,10),ThemeColor(ChooseColor));
			}
			Win->Debug_DisplayBorder(gPS);
		}
	
	public:
		inline unsigned GetLastSwitchEventTimeStamp()
		{return LastSwitchEventTimeStamp;}
		
		BorderCheckBox(const Widgets::WidgetID &_ID,Widgets *_fa,PosizeEX* psex,bool defaultOnOff,void(*_func)(T&,bool),const T &_funcData)
		:CheckBox<T>::CheckBox(_ID,WidgetType_BorderCheckBox,_fa,psex,defaultOnOff,_func,_funcData) {}
		
		BorderCheckBox(const Widgets::WidgetID &_ID,Widgets *_fa,PosizeEX* psex,bool defaultOnOff)
		:CheckBox<T>::CheckBox(_ID,WidgetType_BorderCheckBox,_fa,psex,defaultOnOff) {}
};

void UpdateMainViewSelection(int pos,int from,unsigned timeStamp)//from:0:Click 1:Key 2:User
{
	static int lastRangeSelectBasePos=0;
	if (pos==-1)
		SelectAllMainViewItem(0);
	else if ((SDL_GetModState()&KMOD_SHIFT))
		SelectMainViewItemRange({min(pos,lastRangeSelectBasePos),max(pos,lastRangeSelectBasePos)},1);//??
	else
	{
		MainViewData &mvd=MainView_GetFuncData(pos);
		if (mvd.CB_Choose!=nullptr&&(mvd.CB_Choose->GetType()!=WidgetType_BorderCheckBox||((BorderCheckBox<PFE_Path>*)mvd.CB_Choose)->GetLastSwitchEventTimeStamp()!=timeStamp))
			if ((SDL_GetModState()&KMOD_CTRL))
				SetSelectMainViewItem(pos,2);
			else
			{
				SelectAllMainViewItem(0);
				SetSelectMainViewItem(pos,1);
			}
	}
	lastRangeSelectBasePos=pos;
}
//End of MultiSelect

inline void MainView_ClearContent(bool isRefresh)
{
	if (InRange(CurrentMainView,CurrentMainView_BVT_0,CurrentMainView_BVT_10)||InThisSet(CurrentMainView,CurrentMainView_BVT_RecycleBin))
		BVT_MainView->ClearBlockContent();
	else if (InThisSet(CurrentMainView,CurrentMainView_LVT_Normal,CurrentMainView_LVT_SearchResult))
		LVT_MainView->ClearListContent();
	if (!isRefresh)
		ClearSelectedItem();
}

//RecentFileHighLight Area
int EnableRecentFileHighLight=3;//(Bitmap) 0:None Bit1:Name Bit2:Block
bool HiddenLowestHighLightWhenFull=1;
const int HighLightTotalLevel=3;
PAL_TimeL RecentFileTimeRegion[HighLightTotalLevel]{PAL_TimeL(0,1,0,0),PAL_TimeL(1,0,0,0),PAL_TimeL(7,0,0,0)};

int GetRecentFileHighLightLevel(const PAL_TimeP &t,const PAL_TimeP &cur)
{
	PAL_TimeL l=cur-t;
	for (int i=0;i<HighLightTotalLevel;++i)
		if (l<=RecentFileTimeRegion[i])
			return i;
	return -1;
}

inline RGBA GetRecentFileHighLightColor(int level)
{
	if (InRange(level,0,HighLightTotalLevel)) return RGBA(255-level*64,0,0,255);
	else return RGBA_NONE;
}
//End of RecentFileHighLight

//FileIcon Area
enum
{
	FileIconType_None=0,
	FileIconType_File,
	FileIconType_Dir,
	FileIconType_EmptyDir,
	FileIconType_NormalDrive,
	FileIconType_RemoteDrive,
	FileIconType_UnconnectedDrive,
	FileIconType_CommonExe,
	//above not need path(ignore path), below need path(or aftername).
	FileIconType_TypeFile=10000,//(Back:File);This with above auto stored in CachedItemIcon which will be stored in file when quit.
	FileIconType_Auto,
	FileIconType_DirWithPreview,//(Back:Dir) 
	FileIconType_Picture,//(Back:TypeFile)
	FileIconType_Video,//(Back:TypeFile)
	FileIconType_Document,//(Back:TypeFile)
	FileIconType_Exe,//(Back:WinAPI)
	FileIconType_WinAPI,//(Back:File/Dir)
	
	FileIconType_Flag_NearByPic
};
enum
{
	IconGetMode_None=0,
	IconGetMode_Default,
	IconGetMode_PureWinAPI,
	IconGetMode_AutoMixed//if not exist in Thumb, use WinAPI and save.(?)
//	IconGetMode_ThumbWinAPIOnly,
//	IconGetMode_NonthumbWinAPIOnly,
};
struct FileIconID
{
	int type=0;
	string str;
	
	bool operator < (const FileIconID &tar) const//??
	{
		if (type==tar.type)
			return str<tar.str;
		else return type<tar.type;
	}
	
	FileIconID(int _type=FileIconType_None,const string &_str=""):type(_type),str(_str) {}
};
atomic_int CurrentIconGetMode(IconGetMode_Default);
//bool EnableUserDefineIcon=1;
bool EnableDirWithPreview=0,
	 IconModeAutoMixedUseDefaultThumb=1,
	 DisablePictureFileThumbnail=0,
	 DisableExtractEXEIcon=0;
atomic_bool EnablePreloadNearbyThumbWhenFree(1);
const int MemCachedItemIconDefaultSize=256;//?
const int GetItemIconThreadCount=4;
int MemCachedItemIconSize=MemCachedItemIconDefaultSize;
map <FileIconID,SharedTexturePtr> CachedCommonItemIcon;//All size is 256*256
LRU_LinkHashTable <FileIconID,Doublet <SharedTexturePtr,int> > MemCachedItemIcon(MemCachedItemIconDefaultSize);//Size is not considered yet...(use default:160)
map <FileIconID,vector <PFE_Path> > WaitingGetIconPaths;
PAL_Mutex Mu_GetTypeIcon;

class PicFileManagerClass//Thread safe
{
	protected:
		set <string> PicFileAftername;//copy of AcceptedPictureFormat
		ThumbnailFileV2 *CurThumb=nullptr,
						*SubThumb=nullptr;
		long long hFilesOfCurpath=0;
		PAL_Mutex mu;//Efficiency??
		
		ThumbnailFileV2* ThumbOfBasePath(const string &str)
		{
			string basepath=GetPreviousBeforeBackSlash(str);
			if (basepath=="") return nullptr;
			if (CurThumb!=nullptr&&CurThumb->GetBasePath()==basepath)
				return CurThumb;
			else if (SubThumb!=nullptr&&SubThumb->GetBasePath()==basepath)
				return SubThumb;
			else return nullptr;
		}
		
		ThumbnailFileV2::ThumbnailPicture* GetThumbPicFromPath(const string &path)
		{
			auto thumbFile=ThumbOfBasePath(path);
			if (thumbFile==nullptr)
				return nullptr;
			auto re=thumbFile->Find(GetLastAfterBackSlash(path));
			return re;
		}
		
		bool CurExist(const string &name)
		{return CurThumb->Find(name)!=nullptr;}
		
		static ThumbnailFileV2* OpenThumb(const string &basePath)
		{
			if (basePath=="")
				return nullptr;
			ThumbnailFileV2 *thumbFile=new ThumbnailFileV2();
			CFileIO io(basePath+"\\PAL_Thumb.pthumb",CFileIO::OpenMode_ReadOnly);//??
			if (io.Valid())
			{
				BasicBinIO bio(&io,0);
				if (thumbFile->Read(bio))
					thumbFile->Clear();
			}
			thumbFile->SetBasePath(basePath);
			return thumbFile;
		}
		
		static void WriteThumb(ThumbnailFileV2 *thumbFile)
		{
			if (thumbFile==nullptr)
				return;
			CFileIO io(thumbFile->GetBasePath()+"\\PAL_Thumb.pthumb",CFileIO::OpenMode_RWEmpty);
			if (io.Valid())
			{
				BasicBinIO bio(&io,0);
				if (thumbFile->Write(bio))
					DD[2]<<"Failed to write "<<thumbFile->GetBasePath()+"\\PAL_Thumb.pthumb"<<endl;
				SetFileAttributesW(Utf8ToUnicode(thumbFile->GetBasePath()+"\\PAL_Thumb.pthumb").c_str(),FILE_ATTRIBUTE_HIDDEN);
			}
			else DD[2]<<"Failed to open "<<thumbFile->GetBasePath()+"\\PAL_Thumb.pthumb"<<endl;
		}
		
	public:
		int UpdateThumb(const string &path,ThumbnailFileV2::ThumbnailPicture *thumb)
		{
			int re=1;
			mu.Lock();
			ThumbnailFileV2 *thumbFile=ThumbOfBasePath(path);
			if (thumbFile!=nullptr)
				if (thumbFile->Insert(thumb,1)==0)
					re=0;
			mu.Unlock();
			return re;
		}
		
		SDL_Surface* CopySurface(const string &path)
		{
			SDL_Surface *re=nullptr;
			mu.Lock();
			auto thumb=GetThumbPicFromPath(path);
			if (thumb!=nullptr)
				re=thumb->CopySurface();
			mu.Unlock();
			return re;
		}
		
		SDL_Surface* GetSurfaceLock(const string &path)//if return nullptr, user not need unlock
		{
			mu.Lock();
			auto thumb=GetThumbPicFromPath(path);
			if (thumb==nullptr||thumb->sur==nullptr)
				return mu.Unlock(),nullptr;
			else return thumb->sur;
		}
		
		inline void GetSurfaceOKUnlock()
		{mu.Unlock();}
		
		string GetNextUnloadedPicInBasePath(bool fullpath=1)
		{
			_wfinddatai64_t da;
			string re="";
			mu.Lock();
			if (hFilesOfCurpath!=-1&&CurThumb!=nullptr)
			{
				if (hFilesOfCurpath==0)
					hFilesOfCurpath=_wfindfirsti64(Utf8ToUnicode(CurThumb->GetBasePath()+"\\*").c_str(),&da);
				if (hFilesOfCurpath!=-1)
					while (1)
					{
						if (_wfindnexti64(hFilesOfCurpath,&da)!=0)
						{
							_findclose(hFilesOfCurpath);
							hFilesOfCurpath=-1;
							break;
						}
						string name=DeleteEndBlank(UnicodeToUtf8(da.name));
						if (name!="."&&name!=".."&&!(da.attrib&_A_SUBDIR)&&PicFileAftername.find(Atoa(GetAftername(name)))!=PicFileAftername.end()&&!CurExist(name))
						{
							re=fullpath?CurThumb->GetBasePath()+"\\"+name:name;
							break;
						}
					}
			}			
			mu.Unlock();
			return re;
		}
		
		inline void SetPicFileAftername(const set <string> &se)
		{
			mu.Lock();
			PicFileAftername=se;
			mu.Unlock();
		}
		
		void SetBasePath(const string &basepath)
		{
			if (CurrentIconGetMode!=IconGetMode_Default&&(CurrentIconGetMode!=IconGetMode_AutoMixed||!IconModeAutoMixedUseDefaultThumb))
				return;
			mu.Lock();
			if (CurThumb==nullptr)
				CurThumb=OpenThumb(basepath);
			else if (basepath!=CurThumb->GetBasePath())
			{
				if (SubThumb==nullptr)
					SubThumb=CurThumb,
					CurThumb=OpenThumb(basepath);
				else if (SubThumb->GetBasePath()==basepath)
					swap(SubThumb,CurThumb);
				else
					WriteThumb(SubThumb),
					DeleteToNULL(SubThumb),
					SubThumb=CurThumb,
					CurThumb=OpenThumb(basepath);
				if (NotInSet(hFilesOfCurpath,0,-1))
					_findclose(hFilesOfCurpath);
				hFilesOfCurpath=0;
			}
			mu.Unlock();
		}
		
		void Quit()
		{
			mu.Lock();
			WriteThumb(CurThumb);
			WriteThumb(SubThumb);
			DeleteToNULL(CurThumb);
			DeleteToNULL(SubThumb);
			if (NotInSet(hFilesOfCurpath,0,-1))
				_findclose(hFilesOfCurpath),
				hFilesOfCurpath=0;
			mu.Unlock();
		}
}PicFileManager;

void UpdateCachedItemIcon(const FileIconID &id,const SharedTexturePtr &pic)
{
	if (id.type<FileIconType_Auto)
	{
		auto mp=CachedCommonItemIcon.find(id);
		if (mp!=CachedCommonItemIcon.end())
			mp->second=pic;
		else CachedCommonItemIcon[id]=pic;
	}
	else
	{
		auto data=MemCachedItemIcon.Get(id);
		if (data==nullptr)
			MemCachedItemIcon.Insert(id,{pic,0});
		else data->a=pic;
	}
}

SharedTexturePtr GetCachedItemIcon(const FileIconID &id)
{
	if (id.type<FileIconType_Auto)
	{
		auto mp=CachedCommonItemIcon.find(id);
		if (mp!=CachedCommonItemIcon.end())
			return mp->second;
	}
	else
	{
		auto data=MemCachedItemIcon.Get(id);
		if (data!=nullptr)
			return data->a;
		else if (id.type==FileIconType_Picture)
		{
			SDL_Surface *sur=PicFileManager.GetSurfaceLock(id.str);
			if (sur!=nullptr)
			{
				SharedTexturePtr tex(CreateTextureFromSurface(sur));
				UpdateCachedItemIcon(id,tex);
				PicFileManager.GetSurfaceOKUnlock();
				return tex;
			}
		}
	}
	return SharedTexturePtr();
}

SharedTexturePtr GetRegressiveIcon(const FileIconID &id)
{
	switch (id.type)
	{
		case FileIconType_TypeFile:
			return GetCachedItemIcon({FileIconType_File,""});
		case FileIconType_DirWithPreview:
			return GetCachedItemIcon({FileIconType_Dir,""});
		case FileIconType_Picture:
		case FileIconType_Video:
		case FileIconType_Document:
		case FileIconType_Exe:
		{
			SharedTexturePtr re=GetCachedItemIcon({FileIconType_TypeFile,Atoa(GetAftername(id.str))});
			if (!re)
				return GetRegressiveIcon(FileIconType_CommonExe);
			else return re;
		}
//		case FileIconType_WinAPI://(Back:File/Dir)
		default:	return SharedTexturePtr();
	}
}

SDL_Surface *LoadItemIcon(const FileIconID &id,int size=0,const atomic_int *quitflag=nullptr)//Be careful! It may be run in subthread or mainthread.
{
	SDL_Surface *re=nullptr;
	switch (id.type)
	{
		case FileIconType_None:	break;
		case FileIconType_Auto:	break;
		case FileIconType_DirWithPreview://(Back:Dir)
			break;
		case FileIconType_Picture:
		case FileIconType_Flag_NearByPic:
		{
			auto thumb=ThumbnailFileV2::GetThumbnail(id.str,160,160,quitflag);
			if (thumb!=nullptr)
			{
				if (id.type==FileIconType_Picture)
					re=thumb->CopySurface();//Efficiency??
				if (PicFileManager.UpdateThumb(id.str,thumb)!=0)
					delete thumb;
			}
			break;
		}
		case FileIconType_Video://(Back:TypeFile)
			break;
		case FileIconType_Document://(Back:TypeFile)
			break;
		case FileIconType_WinAPI:
			if (IsVolume(id.str)&&GetDriveType(id.str.c_str())==DRIVE_REMOTE)
				break;
			re=PAL_Platform::GetFileItemThumbnail(id.str,160);
			if (re==nullptr)
				re=PAL_Platform::GetFileItemIcon(id.str,80);
			break;
		default:
		{
			Mu_GetTypeIcon.Lock();
			switch (id.type)
			{
				case FileIconType_File:				re=PAL_Platform::GetCommonIcon_SHGetStockIconInfo(SIID_DOCNOASSOC,3);		break;
				case FileIconType_Dir:				re=PAL_Platform::GetCommonIcon_SHGetStockIconInfo(SIID_STUFFEDFOLDER,3);	break;
				case FileIconType_EmptyDir:			re=PAL_Platform::GetCommonIcon_SHGetStockIconInfo(SIID_FOLDER,3);			break;
				case FileIconType_NormalDrive:		re=PAL_Platform::GetCommonIcon_SHGetStockIconInfo(SIID_DRIVEFIXED,3);		break;
				case FileIconType_RemoteDrive:		re=PAL_Platform::GetCommonIcon_SHGetStockIconInfo(SIID_DRIVENET,3);			break;
				case FileIconType_UnconnectedDrive:	re=PAL_Platform::GetCommonIcon_SHGetStockIconInfo(SIID_DRIVENETDISABLED,3);	break;
				case FileIconType_CommonExe:		re=PAL_Platform::GetCommonIcon_SHGetStockIconInfo(SIID_APPLICATION,3);		break;
				case FileIconType_Exe:				re=PAL_Platform::GetExeFileIcon(id.str,3);									break;
				case FileIconType_TypeFile:
				{
					re=PAL_Platform::GetFileTypeIcon(id.str,3);
					auto CheckIsSmallJumboIcon=[](SDL_Surface *sur)->bool
					{
						if (sur==nullptr)
							return 1;
						if (GetSDLSurfacePixel(sur,{sur->w/2,sur->h/2}).a!=0)
							return 0;
						for (int i=0;i<sur->w;++i)
							if (GetSDLSurfacePixel(sur,{i,sur->h/2}).a!=0)
								return 0;
						for (int i=0;i<sur->h;++i)
							if (GetSDLSurfacePixel(sur,{sur->w/2,i}).a!=0)
								return 0;
						return 1;
					};
					if (CheckIsSmallJumboIcon(re))
					{
						SDL_FreeSurface(re);
						re=PAL_Platform::GetFileTypeIcon(id.str,2);
					}
					break;
				}
			}
			Mu_GetTypeIcon.Unlock();
			break;
		}
	}
	return re;
}

using PTW_Icon_DataType=Doublet <FileIconID,int>;
class PTW_Icon:public PAL_ThreadWorker <PTW_Icon_DataType,GetItemIconThreadCount>
{
	protected:
		using PAL_ThreadWorker<PTW_Icon_DataType,GetItemIconThreadCount>::Lock;
		using PAL_ThreadWorker<PTW_Icon_DataType,GetItemIconThreadCount>::Unlock;
		using PAL_ThreadWorker<PTW_Icon_DataType,GetItemIconThreadCount>::WakeWork;
		
		list <PTW_Icon_DataType> li;
		
		virtual bool Get(PTW_Icon_DataType &data)
		{
			Lock();
			if (li.empty())
			{
				Unlock();//??
				if (InThisSet(CurrentIconGetMode,IconGetMode_Default,IconGetMode_AutoMixed)&&EnablePreloadNearbyThumbWhenFree)
				{
					string path=PicFileManager.GetNextUnloadedPicInBasePath();
					if (path!="")
					{
						data={FileIconID(FileIconType_Flag_NearByPic,path),0};
						return 1;
					}
				}
				else if (CurrentIconGetMode==IconGetMode_PureWinAPI)
				{
					//...
				}
				return 0;
			}
			else
			{
				data=li.front();
				li.pop_front();
				Unlock();
				return 1;
			}
		}
		
	public:
		void Push(const FileIconID &id,const PFE_Path &path,int size=0)
		{
			auto mp=WaitingGetIconPaths.find(id);
			if (mp==WaitingGetIconPaths.end())
			{
				WaitingGetIconPaths[id]=vector<PFE_Path>(1,path);
				Lock();
				li.push_back({id,size});
				Unlock();
				WakeWork();
			}
			else mp->second.push_back(path);
		}
		
		inline void Clear()
		{
			Lock();
			li.clear();
			Unlock();
			WaitingGetIconPaths.clear();
		}
		
		template <typename ...Ts> PTW_Icon(const Ts & ...args):PAL_ThreadWorker<PTW_Icon_DataType,GetItemIconThreadCount>(args...) {}	
}ThreadGetItemIcon([](PTW_Icon_DataType &data,const atomic_int &quitflag,int)
{
	SDL_Surface *sur=LoadItemIcon(data.a,data.b,&quitflag);
	if (sur!=nullptr)
		PUI_SendFunctionEvent<Doublet<FileIconID,SDL_Surface*> >([](Doublet<FileIconID,SDL_Surface*> &data)
		{
			SharedTexturePtr tex(CreateTextureFromSurfaceAndDelete(data.b));
			UpdateCachedItemIcon(data.a,tex);
			auto mp=WaitingGetIconPaths.find(data.a);
			if (mp!=WaitingGetIconPaths.end())
			{
				for (const auto &vp:mp->second)
					MainView_UpdateIcon(vp,tex,data.a.type==FileIconType_WinAPI?20:10);
				WaitingGetIconPaths.erase(mp);
			}
		},{data.a,sur});
});

SharedTexturePtr GetItemIcon(const PFE_Path &path,unsigned int size=0,int icontype=FileIconType_Auto,int cacheMode=0)//cacheMode0:Cache-Thread 1:Block(Supported uncompletely...) 2:CacheOnly 3:Cache-Thread,but no extra file
{
	if (CurrentIconGetMode==IconGetMode_None)
		return SharedTexturePtr();
	FileIconID id(icontype,path.str),id2;
	if (icontype==FileIconType_Auto)
	{
		if (CurrentIconGetMode==IconGetMode_AutoMixed&&InThisSet(path.type,PFE_PathType_Dir,PFE_PathType_File,PFE_PathType_Volume))
			id2=FileIconID(FileIconType_WinAPI,path.str);
		if (CurrentIconGetMode==IconGetMode_PureWinAPI&&InThisSet(path.type,PFE_PathType_Dir,PFE_PathType_File,PFE_PathType_Volume))
			id.type=FileIconType_WinAPI;
		else switch (path.type)
		{
			case PFE_PathType_Volume:
				id=FileIconID(path.code==PFE_Path_VolumeCode_Remote?FileIconType_UnconnectedDrive:FileIconType_NormalDrive,"");
				break;
			case PFE_PathType_Dir:
				if (IsDirEmpty(path.str))
					id=FileIconID(FileIconType_EmptyDir,"");
				else if (cacheMode!=3&&EnableDirWithPreview)
					id.type=FileIconType_DirWithPreview;
				else id=FileIconID(FileIconType_Dir,"");
				break;
			case PFE_PathType_File:
			{
				string aftername=Atoa(GetAftername(GetLastAfterBackSlash(path.str)));
				if (aftername=="")
					id=FileIconID(FileIconType_File,"");
				if (!DisableExtractEXEIcon&&InThisSet(aftername,".exe",".msi"))
					id.type=FileIconType_Exe;
				else if (cacheMode!=3&&!DisablePictureFileThumbnail&&AcceptedPictureFormat.find(aftername)!=AcceptedPictureFormat.end())
					id.type=FileIconType_Picture;
//				else if ()
//					;
				else id=FileIconID(FileIconType_TypeFile,aftername);
				break;
			}
			default:	return SharedTexturePtr();
		}
	}
	
	SharedTexturePtr re;
	if (CurrentIconGetMode==IconGetMode_AutoMixed&&(!IconModeAutoMixedUseDefaultThumb||NotInSet(id.type,FileIconType_Picture)))
	{
		re=GetCachedItemIcon(id2);
		if (!re)
			if (cacheMode==0||cacheMode==3)
				ThreadGetItemIcon.Push(id2,path,size);
			else if (cacheMode==1)
			{
				SDL_Surface *sur=LoadItemIcon(id2,size);
				if (sur!=nullptr)
				{
					re=SharedTexturePtr(CreateTextureFromSurfaceAndDelete(sur));
					UpdateCachedItemIcon(id2,re);
					return re;
				}
			}
	}
	if (!re)
		re=GetCachedItemIcon(id);
	if (!re&&(CurrentIconGetMode!=IconGetMode_AutoMixed||NotInSet(id.type,FileIconType_Picture)||IconModeAutoMixedUseDefaultThumb))
		if (cacheMode==0||cacheMode==3)
			ThreadGetItemIcon.Push(id,path,size);
		else if (cacheMode==1)
		{
			SDL_Surface *sur=LoadItemIcon(id,size);
			if (sur!=nullptr)
			{
				re=SharedTexturePtr(CreateTextureFromSurfaceAndDelete(sur));
				UpdateCachedItemIcon(id,re);
			}
		}
	if (!re)
		re=GetRegressiveIcon(id);
	return re;
}

void SwitchGetIconMode(int mode)
{
	if (mode==CurrentIconGetMode) return;
	ThreadGetItemIcon.Clear();
	if (mode==IconGetMode_None)
	{
		PicFileManager.Quit();
		CachedCommonItemIcon.clear();
		MemCachedItemIcon.Clear();
	}
	else if (mode==IconGetMode_PureWinAPI)
		PicFileManager.Quit();
	CurrentIconGetMode=mode;
}

inline void SetMemCachedItemIconSize(unsigned size=MemCachedItemIconDefaultSize)
{
	MemCachedItemIconSize=size;
	MemCachedItemIcon.SetSizeLimit(MemCachedItemIconSize);
	PFE_Cfg("MemCachedItemIconSize")=llTOstr(MemCachedItemIconSize);
}

int GetItemIconInit()
{
	DD[4]<<"GetItemIconInit"<<endl;
	string s;
	if (strISull(s=PFE_Cfg("CurrentIconGetMode")))
		CurrentIconGetMode=strTOint(s);
	if (strISull(s=PFE_Cfg("IconModeAutoMixedUseDefaultThumb")))
		IconModeAutoMixedUseDefaultThumb=s!="0";
	if (strISull(s=PFE_Cfg("DisablePictureFileThumbnail")))
		DisablePictureFileThumbnail=s=="1";
	if (strISull(s=PFE_Cfg("DisableExtractEXEIcon")))
		DisableExtractEXEIcon=s=="1";
	if (strISull(s=PFE_Cfg("EnablePreloadNearbyThumbWhenFree")))
		EnablePreloadNearbyThumbWhenFree=s!="0";
	if (strISull(s=PFE_Cfg("MemCachedItemIconSize")))
		MemCachedItemIconSize=strTOll(s);
	ThreadGetItemIcon.Start();
	DD[5]<<"GetItemIconInit"<<endl;
	return 0;
}

void GetItemIconQuit()
{
	DD[4]<<"GetItemIconQuit"<<endl;
	ThreadGetItemIcon.Quit();
	CachedCommonItemIcon.clear();
	MemCachedItemIcon.Clear();
	PicFileManager.Quit();
	DD[5]<<"GetItemIconQuit"<<endl;
}
//End of FileIcon

//RecycleBin Area
struct DeletedItemMetaData
{
	int Version=1;
	PAL_TimeP t;
	string RawPath;
	int AutoDeleteTimeout=0;//0:Follow global -1:Never
	int AutoDeleteWhenNeeded=0;//0:Follow global -1:Never
};

inline BasicBinIO& operator << (BasicBinIO &bio,const DeletedItemMetaData &tar)
{return bio<<tar.Version<<tar.t<<tar.RawPath<<tar.AutoDeleteTimeout<<tar.AutoDeleteWhenNeeded;}

inline BasicBinIO& operator >> (BasicBinIO &bio,DeletedItemMetaData &tar)
{return bio>>tar.Version>>tar.t>>tar.RawPath>>tar.AutoDeleteTimeout>>tar.AutoDeleteWhenNeeded;}

enum
{
	RecycleBinAllowedOperation_OpenFile=1,
	RecycleBinAllowedOperation_OpenDir=2,
	RecycleBinAllowedOperation_Copy=4,
	RecycleBinAllowedOperation_Move=8,
	RecycleBinAllowedOperation_Delete=16,
	RecycleBinAllowedOperation_Rename=32,
	RecycleBinAllowedOperation_SelectInWinExplorer=64,
	RecycleBinAllowedOperation_ShellContextMenu=128,
	RecycleBinAllowedOperation_WinProperty=256,
	RecycleBinAllowedOperation_Property=512
};
int RecycleBinAllowedOperation=RecycleBinAllowedOperation_Delete|RecycleBinAllowedOperation_Copy|RecycleBinAllowedOperation_Property;

vector <string> RecycleBinPathToVolume(const PFE_Path &recBin)
{
	vector <string> TargetVolume;
	if (recBin.type==PFE_PathType_RecycleBin)
		if (recBin.code==PFE_Path_RecycleBinCode_Volume)
			TargetVolume.push_back(recBin.str);
		else for (const auto &vp:GetAllLogicalDrive())
			switch (GetDriveType(vp.c_str()))
			{
				case DRIVE_REMOTE://It may be slow!
					if (recBin.type==PFE_Path_RecycleBinCode_All)
						TargetVolume.push_back(vp);
					break;
				case DRIVE_UNKNOWN:
					break;
				default:
					TargetVolume.push_back(vp);
					break;
			}
	return TargetVolume;
}

void GetAllFileInRecycleBin(vector <PFE_Path> &dst,const PFE_Path &recBin)
{
	vector <string> TargetVolume=RecycleBinPathToVolume(recBin);
	for (const auto &vp:TargetVolume)
	{
		vector <PFE_Path> vec;
		GetAllFileInDir(PFE_Path(PFE_PathType_Dir,vp+"\\PAL_RecycleBin","PAL_RecycleBin"),vec,GetAllFileInDir_To_RecycleBin);
		for (const auto &vpp:vec)
			if (vpp.type==PFE_PathType_Dir)
				GetAllFileInDir(vpp,dst,GetAllFileInDir_To_RecycleBin);
	}
}

void RestoreRecycledFile(const PFE_Path &tar)
{
	string RecycleBinID=GetPreviousBeforeBackSlash(tar.str);
	CFileIO io(RecycleBinID+".del",CFileIO::OpenMode_ReadOnly);
	if (io.Valid())
	{
		DeletedItemMetaData dimd;
		BasicBinIO bio(io);
		bio>>dimd;
		io.Close();
		vector <string> vec(1,tar.str);
		if (CreateDirectoryR(dimd.RawPath))
			WarningMessageButton(SetSystemBeep_Error,PUIT("错误"),PUIT("无法创建还原原目录！"));
		else AddFileOperationTask(vec,dimd.RawPath,FileOperationType_Move/*??*/);
	}
	else WarningMessageButton(SetSystemBeep_Error,PUIT("错误"),PUIT("无法打开该目标的删除信息文件！"));
}

void ClearRecycleBin(const PFE_Path &recBin)
{
	vector <string> TargetVolume=RecycleBinPathToVolume(recBin);
	for (const auto &vp:TargetVolume)
	{
		vector <PFE_Path> vec;
		vector <string> src;
		if (GetAllFileInDir(PFE_Path(PFE_PathType_Dir,vp+"\\PAL_RecycleBin","PAL_RecycleBin"),vec,GetAllFileInDir_To_RecycleBin)==0)
			continue;
		for (const auto &vpp:vec)
			src.push_back(vpp.str);
		AddFileOperationTask(src,"",FileOperationType_Delete);
	}
}
//End of RecycleBin

//FileNodeOperation Area
class OperationTargets
{
	protected:
		vector <PFE_Path> vec;//size:0:Global or none 1:Single >=2:Multi 
		
	public:
		enum
		{
			Global=0,
			Single,
			Multi
		};
		
		inline bool IsGlobal() const
		{return vec.size()==0;}
		
		inline bool IsSingle() const
		{return IsGlobal()&&!IsMainViewMultiSelected()||vec.size()==1;}
		
		inline bool IsMulti() const
		{return IsGlobal()&&IsMainViewMultiSelected()||vec.size()>=2;}
		
		inline int Type() const
		{return min((int)vec.size(),2);}
		
		inline int Size() const
		{return vec.size()==0?MainViewItemSelectCount():(int)vec.size();}
		
		inline PFE_Path Path() const//??
		{return IsGlobal()?(IsMainViewSingleSelected()?GetFirstSelectedItem():CurrentPath):vec[0];}
		
		inline const decltype(vec)& Paths() const
		{return vec;}
		
		inline unsigned SingleFileTypeMask() const
		{
			switch (Path().type)
			{
				case PFE_PathType_File:			return FileTypeMask_File;
				case PFE_PathType_Dir:			return FileTypeMask_Dir;
				case PFE_PathType_Library:		return FileTypeMask_Library;
				case PFE_PathType_VirtualFile:	return FileTypeMask_VirtualFile;
				case PFE_PathType_VirtualDir:	return FileTypeMask_VirtualDir;
				case PFE_PathType_Volume:		return FileTypeMask_Volume;
				default:						return FileTypeMask_Unknown;
			}
		}
		
		inline unsigned FileTypeMask() const
		{
			if (IsSingle())
				return SingleFileTypeMask();
			else if (IsGlobal())
				return SelectedFileTypeMask;
			else return GetFileTypeMask(vec);
		}
		 
		void DoForAll(void (*func)(const PFE_Path&)) const
		{
			if (IsSingle())
				func(Path());
			else if (IsGlobal())
				for (const auto &vp:SelectedMainViewData)
					func(vp);
			else for (const auto &vp:vec)
					func(vp);
		}
		 
		template <class T> void DoForAllT(void (*func)(T,const PFE_Path&),T funcdata) const
		{
			if (IsSingle())
				func(funcdata,Path());
			else if (IsGlobal())
				for (const auto &vp:SelectedMainViewData)
					func(funcdata,vp);
			else for (const auto &vp:vec)
					func(funcdata,vp);
		}
		
		OperationTargets() {}
		OperationTargets(const PFE_Path &path):vec(1,path) {}
		OperationTargets(const vector <PFE_Path> &paths):vec(paths) {}
};

class OperationContext
{
	protected:
		int	from=0,
			mainView=0;
	
	public:
		inline int MainView() const
		{return mainView==0?(int)CurrentMainView:mainView;}
		
		inline int From() const
		{return from;}
		
		inline static const PFE_Path& CurrentPath()
		{return ::CurrentPath;}
		
		OperationContext(int _from,int _mainview=0):from(_from),mainView(_mainview) {}
		OperationContext() {}
};

class BaseNodeOperation
{
	public:
		enum
		{
			From_User=0,
			From_TopButton,
			From_MainViewMenu,
			From_MainViewBackground,
			From_QuickListMenu,
			From_RecentListMenu
		};
		
		enum
		{
			InvalidBit_User=0,
			InvalidBit_MainView,
			InvalidBit_PathType,
			InvalidBit_Path,
			InvalidBit_SingleMulti,
			InvalidBit_From
		};
		
		inline static int InvalidFlag(int flagBit)
		{return 1<<flagBit;}

		static bool CheckValidBeforeFunc;
		
		virtual string Name() const=0;
		virtual int InvalidFlags(const OperationTargets&,const OperationContext&) const =0;
		virtual void Func(const OperationTargets&,const OperationContext&) const =0;
		//The parameter of Func should be completely same as InvalidFlags;
		//Func won't check valid flag(of Context,if not in context,it may be checked again), error may happen if not checked before.
		
		virtual string Name(const OperationTargets&,const OperationContext&) const
		{return Name();}
		
		inline bool Valid(const OperationTargets &tar,const OperationContext &con) const
		{return !InvalidFlags(tar,con);}
		
		inline void ValidDo(const OperationTargets &tar,const OperationContext &con) const
		{
			if (CheckValidBeforeFunc||Valid(tar,con))
				Func(tar,con);
		}
};
bool BaseNodeOperation::CheckValidBeforeFunc=0;

#define OperationFuncCheckInvalid 				\
	if (CheckValidBeforeFunc&&!Valid(tar,con))	\
		return;

class Operation_OpenSpecificFile:public BaseNodeOperation
{
	public:
		virtual string Name() const {return PUIT("打开");}
		
		virtual int InvalidFlags(const OperationTargets &tar,const OperationContext &con) const
		{
			if (!tar.IsSingle())
				return InvalidFlag(InvalidBit_SingleMulti);
			if (NotInSet(con.From(),From_User,From_TopButton,From_MainViewMenu))
				return InvalidFlag(InvalidBit_From);
			if (tar.Path()==con.CurrentPath())
				return InvalidFlag(InvalidBit_Path);
			if (NotInSet(tar.Path().type,PFE_PathType_File))
				return InvalidFlag(InvalidBit_PathType);
			if (tar.IsGlobal()&&con.MainView()==CurrentMainView_BVT_RecycleBin&&!(RecycleBinAllowedOperation&RecycleBinAllowedOperation_OpenFile))
				return InvalidFlag(InvalidBit_MainView);
			return 0;
		}
		
		virtual void Func(const OperationTargets &tar,const OperationContext &con) const
		{
			OperationFuncCheckInvalid;
			SystemOpenSpecificFile(tar.Path().str);
		}
};

class Operation_OpenMultiFile:public BaseNodeOperation
{
	public:
		virtual string Name() const {return PUIT("全部打开");}
		
		virtual int InvalidFlags(const OperationTargets &tar,const OperationContext &con) const
		{
			if (!tar.IsMulti())
				return InvalidFlag(InvalidBit_SingleMulti);
			if (NotInSet(con.From(),From_User,From_TopButton,From_MainViewMenu))
				return InvalidFlag(InvalidBit_From);
			if (tar.FileTypeMask()!=FileTypeMask_File)
				return InvalidFlag(InvalidBit_PathType);
			if (tar.IsGlobal()&&con.MainView()==CurrentMainView_BVT_RecycleBin&&!(RecycleBinAllowedOperation&RecycleBinAllowedOperation_OpenFile))
				return InvalidFlag(InvalidBit_MainView);
			return 0;
		}
		
		virtual void Func(const OperationTargets &tar,const OperationContext &con) const
		{
			OperationFuncCheckInvalid;
			tar.DoForAll([](const PFE_Path &path)
			{
				SystemOpenSpecificFile(path.str);
			});
		}
};

class Operation_EnterPath:public BaseNodeOperation
{
	public:
		virtual string Name() const {return PUIT("打开");}
		
		virtual int InvalidFlags(const OperationTargets &tar,const OperationContext &con) const
		{
			if (!tar.IsSingle())
				return InvalidFlag(InvalidBit_SingleMulti);
			if (NotInSet(con.From(),From_TopButton,From_MainViewMenu,From_QuickListMenu,From_RecentListMenu))
				return InvalidFlag(InvalidBit_From);
			if (tar.Path()==con.CurrentPath())
				return InvalidFlag(InvalidBit_Path);
			if (InThisSet(tar.Path().type,PFE_PathType_VirtualFile))
				return InvalidFlag(InvalidBit_PathType);
			if (tar.IsGlobal()&&con.MainView()==CurrentMainView_BVT_RecycleBin&&!(RecycleBinAllowedOperation&RecycleBinAllowedOperation_OpenDir))
				return InvalidFlag(InvalidBit_MainView);
			return 0;
		}
		
		virtual void Func(const OperationTargets &tar,const OperationContext &con) const
		{
			OperationFuncCheckInvalid;
			switch (con.From())//??
			{
				case From_MainViewMenu:
					SetCurrentPath(tar.Path(),SetCurrentPathFrom_MainView);
					break;
				case From_QuickListMenu:
				case From_RecentListMenu:
					SetCurrentPath(tar.Path(),SetCurrentPathFrom_LeftList);
					break;
				default:
					SetCurrentPath(tar.Path(),SetCurrentPathFrom_User);
					break;
			}
		}
};

class Operation_OpenInNewTab:public BaseNodeOperation
{
	public:
		virtual string Name() const {return PUIT("在新标签页打开");}
		
		virtual int InvalidFlags(const OperationTargets &tar,const OperationContext &con) const
		{
			if (CurrentTabMode==TabMode_None)
				return InvalidFlag(InvalidBit_User);
			if (!tar.IsSingle())
				return InvalidFlag(InvalidBit_SingleMulti);
			if (InThisSet(tar.Path().type,PFE_PathType_File,PFE_PathType_VirtualFile,PFE_PathType_None))
				return InvalidFlag(InvalidBit_PathType);
			if (InThisSet(con.From(),From_User,From_TopButton,From_MainViewMenu)&&con.MainView()==CurrentMainView_BVT_RecycleBin&&!(RecycleBinAllowedOperation&RecycleBinAllowedOperation_OpenDir))
				return InvalidFlag(InvalidBit_MainView);
			return 0;
		}
		
		virtual void Func(const OperationTargets &tar,const OperationContext &con) const
		{
			OperationFuncCheckInvalid;
			if (CurrentTabMode!=TabMode_None)//Necessary?
				TM_Tab->AddTab(1e9,new PathContext(tar.Path()),tar.Path().name);
		}
};

class Operation_CopyItemPath:public BaseNodeOperation
{
	public:
		virtual string Name() const {return PUIT("复制路径");}
		
		virtual int InvalidFlags(const OperationTargets &tar,const OperationContext &con) const
		{
			if (!IsPureDirFileVol(tar.FileTypeMask()))
				return InvalidFlag(InvalidBit_PathType);
			if (tar.IsGlobal()&&con.MainView()==CurrentMainView_BVT_RecycleBin)
				return InvalidFlag(InvalidBit_MainView);
			return 0;
		}
		
		virtual void Func(const OperationTargets &tar,const OperationContext &con) const
		{
			OperationFuncCheckInvalid;
			string s;
			tar.DoForAllT<string*>([](string *s,const PFE_Path &path)
			{
				if (*s!="")
					*s+="\n";
				*s+=path.str;
			},&s);
			SDL_SetClipboardText(s.c_str());
		}
		
		Operation_CopyItemPath(int)
		{ValidDo(OperationTargets(),OperationContext());}
		
		Operation_CopyItemPath() {}
};

class Operation_SetPicAsBackground:public BaseNodeOperation
{
	public:
		virtual string Name() const {return PUIT("设置为背景图片");}
		
		virtual int InvalidFlags(const OperationTargets &tar,const OperationContext &con) const
		{
			if (!tar.IsSingle())
				return InvalidFlag(InvalidBit_SingleMulti);
			if (NotInSet(tar.Path().type,PFE_PathType_Dir,PFE_PathType_File,PFE_PathType_Volume))
				return InvalidFlag(InvalidBit_PathType);
			if (tar.IsGlobal()&&con.MainView()==CurrentMainView_BVT_RecycleBin)
				return InvalidFlag(InvalidBit_MainView);
			if (ShowSettingBackgroundPicOnRightClickMenu&&tar.Path().type==PFE_PathType_File&&AcceptedPictureFormat.find(Atoa(GetAftername(tar.Path().str)))!=AcceptedPictureFormat.end())
				return 0;
			else return InvalidFlag(InvalidBit_User);
		}
		
		virtual void Func(const OperationTargets &tar,const OperationContext &con) const
		{
			OperationFuncCheckInvalid;
			SetBackgroundPic(tar.Path().str,0);
		}
};

class Operation_Refresh:public BaseNodeOperation
{
	public:
		virtual string Name() const {return PUIT("刷新");}
		
		virtual int InvalidFlags(const OperationTargets &tar,const OperationContext &con) const
		{
			if (NotInSet(con.From(),From_User,From_TopButton,From_MainViewBackground))
				return InvalidFlag(InvalidBit_From);
			if (InThisSet(con.CurrentPath().type,PFE_PathType_Setting,PFE_PathType_About))
				return InvalidFlag(InvalidBit_User);
			return 0;
		}
		
		virtual void Func(const OperationTargets &tar,const OperationContext &con) const
		{
			OperationFuncCheckInvalid;
			double pos=Larlay_Centre->GetScrollBarPercentY();
			SetCurrentPath(CurrentPath,SetCurrentPathFrom_Refresh);
			Larlay_Centre->SetViewPort(4,pos);
		}
		
		Operation_Refresh(int)
		{ValidDo(OperationTargets(),OperationContext());}
		
		Operation_Refresh() {}
};

inline void RefreshCurrentPath()
{Operation_Refresh(0);}

class Operation_Cut:public BaseNodeOperation
{
	public:
		virtual string Name() const {return PUIT("剪切");}
		
		virtual int InvalidFlags(const OperationTargets &tar,const OperationContext &con) const
		{
			if (NotInSet(con.From(),From_User,From_TopButton,From_MainViewMenu))
				return InvalidFlag(InvalidBit_From);
			if (tar.Size()==0)
				return InvalidFlag(InvalidBit_Path);
			if (!IsPureDirFile(tar.FileTypeMask()))
				return InvalidFlag(InvalidBit_PathType);
			if (!CurrentMainViewIsNormal(con.MainView()))
				return InvalidFlag(InvalidBit_MainView);
			if (tar.IsGlobal()&&con.MainView()==CurrentMainView_BVT_RecycleBin&&!(RecycleBinAllowedOperation&RecycleBinAllowedOperation_Move))
				return InvalidFlag(InvalidBit_MainView);
			return 0;
		}
		
		virtual void Func(const OperationTargets &tar,const OperationContext &con) const
		{
			OperationFuncCheckInvalid;
			FileNodeClipboard.clear();
			CoMoClipboardType=1;
			tar.DoForAll([](const PFE_Path &path)
			{
//				if (InThisSet(vp.type,PFE_PathType_Dir,PFE_PathType_File))//??
					FileNodeClipboard.push_back(path);
			});
			SyncInnerClipboardToSystemClipboard();
		}
		
		Operation_Cut(int)
		{ValidDo(OperationTargets(),OperationContext());}
		
		Operation_Cut() {}
};

class Operation_Copy:public BaseNodeOperation
{
	public:
		virtual string Name() const {return PUIT("复制");}
		
		virtual int InvalidFlags(const OperationTargets &tar,const OperationContext &con) const
		{
			if (!IsPureDirFile(tar.FileTypeMask()))
				return InvalidFlag(InvalidBit_PathType);
			if (con.From()==From_MainViewBackground&&InThisSet(con.MainView(),CurrentMainView_BVT_RecycleBin,CurrentMainView_LVT_SearchResult))
				return InvalidFlag(InvalidBit_MainView);
			if (!CurrentMainViewIsNormal(con.MainView()))
				return InvalidFlag(InvalidBit_MainView);
			if (tar.IsGlobal()&&con.MainView()==CurrentMainView_BVT_RecycleBin&&!(RecycleBinAllowedOperation&RecycleBinAllowedOperation_Copy))
				return InvalidFlag(InvalidBit_MainView);
			return 0;
		}
		
		virtual void Func(const OperationTargets &tar,const OperationContext &con) const
		{
			OperationFuncCheckInvalid;
			FileNodeClipboard.clear();
			CoMoClipboardType=0;
			tar.DoForAll([](const PFE_Path &path)
			{
//				if (InThisSet(vp.type,PFE_PathType_Dir,PFE_PathType_File))//??
					FileNodeClipboard.push_back(path);
			});
			SyncInnerClipboardToSystemClipboard();
		}
		
		Operation_Copy(int)
		{ValidDo(OperationTargets(),OperationContext());}
		
		Operation_Copy() {}
};

class Operation_Paste:public BaseNodeOperation
{
	public:
		virtual string Name() const {return PUIT("粘贴");}
		
		virtual int InvalidFlags(const OperationTargets &tar,const OperationContext &con) const
		{
			if (!tar.IsSingle())
				return InvalidFlag(InvalidBit_SingleMulti);
			if (NotInSet(tar.Path().type,PFE_PathType_Dir,PFE_PathType_Volume,PFE_PathType_Library))
				return InvalidFlag(InvalidBit_PathType);
			if (!CurrentMainViewIsNormal(con.MainView())||con.MainView()==CurrentMainView_BVT_RecycleBin)
				return InvalidFlag(InvalidBit_MainView);
			if (con.From()==From_MainViewBackground&&InThisSet(con.MainView(),CurrentMainView_LVT_SearchResult))
				return InvalidFlag(InvalidBit_MainView);
			if (tar.Path().str==""||SyncSystemClipboardToInnerClipboard()!=0||FileNodeClipboard.empty())
				return InvalidFlag(InvalidBit_User);
			return 0;
		}
		
		virtual void Func(const OperationTargets &tar,const OperationContext &con) const
		{
			OperationFuncCheckInvalid;
			vector <string> src;
			for (const auto &vp:FileNodeClipboard)
				src.push_back(vp.str);
			AddFileOperationTask(src,tar.Path().str,CoMoClipboardType==0?FileOperationType_Copy:FileOperationType_Move);
			FileNodeClipboard.clear();
			SyncInnerClipboardToSystemClipboard();//??
		}
		
		Operation_Paste(int)
		{ValidDo(OperationTargets(),OperationContext());}
		
		Operation_Paste() {}
};

class Operation_Recycle:public BaseNodeOperation
{
	public:
		virtual string Name() const {return PUIT("删除");}
		
		virtual int InvalidFlags(const OperationTargets &tar,const OperationContext &con) const
		{
			if (NotInSet(con.From(),From_User,From_TopButton,From_MainViewMenu))
				return InvalidFlag(InvalidBit_From);
			if (!CurrentMainViewIsNormal(con.MainView())||con.MainView()==CurrentMainView_BVT_RecycleBin)
				return InvalidFlag(InvalidBit_MainView);
			if (tar.Size()==0)
				return InvalidFlag(InvalidBit_Path);
			if (!IsPureDirFile(tar.FileTypeMask()))
				return InvalidFlag(InvalidBit_PathType);
			return 0;
		}
		
		virtual void Func(const OperationTargets &tar,const OperationContext &con) const
		{
			OperationFuncCheckInvalid;
			map <string,vector <string> > srcs;
			tar.DoForAllT<decltype(srcs)*>([](decltype(srcs) *Srcs,const PFE_Path &path)
			{
				auto &srcs=*Srcs;
				string base=GetPreviousBeforeBackSlash(path.str);//??
				if (base!="")
					if (srcs.find(base)==srcs.end())
						srcs[base]=vector <string> (1,path.str);
					else srcs[base].push_back(path.str);
			},&srcs);
			for (auto &mp:srcs)
			{
				string vol=GetBaseVolume(mp.first);
				string RecycleBinPath=vol+PLATFORM_PATHSEPERATOR+"PAL_RecycleBin";
				if (!IsFileExsit(RecycleBinPath,1))
					if (CreateDirectoryW(Utf8ToUnicode(RecycleBinPath).c_str(),NULL)==0)
					{
						WarningMessageButton(SetSystemBeep_Error,PUIT("错误"),PUIT("无法在 "+vol+" 创建回收站！"));
						return;
					}
					else SetFileAttributesW(Utf8ToUnicode(RecycleBinPath).c_str(),FILE_ATTRIBUTE_HIDDEN);
				vector <string> &vec=mp.second;
				for (int i=0;i<vec.size();++i)
					while (i<vec.size()&&GetSamePrefix(vec[i],RecycleBinPath)==RecycleBinPath)
						vec.erase(vec.begin()+i);
				if (vec.empty())
					continue;
				PAL_TimeP t=PAL_TimeP::CurrentDateTime();
				string dirname=llTOstr(t.year,4)+llTOstr(t.month,2)+llTOstr(t.day,2)+llTOstr(t.hour,2)+llTOstr(t.minute,2)+llTOstr(t.second,2)+"-01234567";
				int retryCnt=10;
				do for (int i=1,r;i<=8;++i)
					if (InRange(r=rand()%36,0,25))
						dirname[dirname.length()-i]='A'+r;
					else dirname[dirname.length()-i]=r-26+'0';
				while (CreateDirectoryW(Utf8ToUnicode(RecycleBinPath+PLATFORM_PATHSEPERATOR+dirname).c_str(),NULL)==0&&retryCnt-->0);
				DeletedItemMetaData dimd;
				dimd.t=t;
				dimd.RawPath=mp.first;
				while (retryCnt>0)
				{
					CFileIO io(RecycleBinPath+PLATFORM_PATHSEPERATOR+dirname+".del",CFileIO::OpenMode_RWEmpty);
					if (io.Valid())
					{
						BasicBinIO bio(&io,0);
						bio<<dimd;
						break;
					}
					else --retryCnt;
				}
				if (retryCnt<=0)
				{
					WarningMessageButton(SetSystemBeep_Error,PUIT("错误"),PUIT("创建回收站元信息文件失败！"));
					return;
				}
				AddFileOperationTask(vec,RecycleBinPath+PLATFORM_PATHSEPERATOR+dirname,FileOperationType_Recycle);
			}
		}
		
		Operation_Recycle(int)
		{ValidDo(OperationTargets(),OperationContext());}
		
		Operation_Recycle() {}
};

class Operation_Delete:public BaseNodeOperation
{
	public:
		virtual string Name() const {return PUIT("永久删除");}
		
		virtual int InvalidFlags(const OperationTargets &tar,const OperationContext &con) const
		{
			if (NotInSet(con.From(),From_User,From_TopButton,From_MainViewMenu))
				return InvalidFlag(InvalidBit_From);
			if (con.From()==From_MainViewMenu&&!ShowRealDeleteInRigthClickMenu)
				return InvalidFlag(InvalidBit_From);
			if (!CurrentMainViewIsNormal(con.MainView()))
				return InvalidFlag(InvalidBit_MainView);
			if (tar.Size()==0)
				return InvalidFlag(InvalidBit_Path);
			if (!IsPureDirFile(tar.FileTypeMask()))
				return InvalidFlag(InvalidBit_PathType);
			if (tar.IsGlobal()&&con.MainView()==CurrentMainView_BVT_RecycleBin&&!(RecycleBinAllowedOperation&RecycleBinAllowedOperation_Delete))
				return InvalidFlag(InvalidBit_MainView);
			return 0;
		}
		
		virtual void Func(const OperationTargets &tar,const OperationContext &con) const
		{
			OperationFuncCheckInvalid;
			vector <string> src;
			tar.DoForAllT<decltype(src)*>([](decltype(src) *Src,const PFE_Path &path)
			{
				Src->push_back(path.str);
			},&src);
			AddFileOperationTask(src,"",FileOperationType_Delete);
		}
		
		Operation_Delete(int)
		{ValidDo(OperationTargets(),OperationContext());}
		
		Operation_Delete() {}
};

class Operation_Rename:public BaseNodeOperation
{
	public:
		virtual string Name() const {return PUIT("重命名");}
		
		virtual int InvalidFlags(const OperationTargets &tar,const OperationContext &con) const
		{
			if (!tar.IsSingle())
				return InvalidFlag(InvalidBit_SingleMulti);
			if (NotInSet(con.From(),From_User,From_TopButton,From_MainViewMenu))
				return InvalidFlag(InvalidBit_From);
			if (tar.Size()==0)
				return InvalidFlag(InvalidBit_Path);
			if (!IsPureDirFileVol(tar.FileTypeMask()))
				return InvalidFlag(InvalidBit_PathType);
			if (!CurrentMainViewIsNormal(con.MainView()))
				return InvalidFlag(InvalidBit_MainView);
			if (tar.IsGlobal()&&con.MainView()==CurrentMainView_BVT_RecycleBin&&!(RecycleBinAllowedOperation&RecycleBinAllowedOperation_Rename))
				return InvalidFlag(InvalidBit_MainView);
			return 0;
		}
		
		virtual void Func(const OperationTargets &tar,const OperationContext &con) const
		{
			OperationFuncCheckInvalid;
			const PFE_Path &path=tar.Path();
			auto mbl=new MessageBoxLayer(0,DeleteEndBlank(PUIT("重命名："))+path.name,400,80);
			mbl->SetClickOutsideReaction(1);
			mbl->EnableShowTopAreaColor(1);
			mbl->SetBackgroundColor(DUC_MessageLayerBackground);
			mbl->SetTopAreaColor(DUC_MessageLayerTopAreaColor);
			auto tel=new TextEditLine <Doublet <PFE_Path,MessageBoxLayer*> > (0,mbl,new PosizeEX_Fa6(2,2,20,85,40,10),
				[](auto &funcdata,const stringUTF8 &strutf8,auto *tel,bool isenter)
				{
					if (isenter)
					{
						string str=strutf8.cppString(),newpath;
						if (!str.empty())
							if (str.size()>256)
								WarningMessageButton(SetSystemBeep_Error,PUIT("错误！"),PUIT("文件名过长！"));
							else
							{
								bool invalidflag=0;
								for (int i=0;i<str.length()&&!invalidflag;++i)
									for (int j=0;j<9&&!invalidflag;++j)
										if (str[i]==InvalidFileName[j])
											invalidflag=1;
								if (invalidflag)
									WarningMessageButton(SetSystemBeep_Error,PUIT("错误！"),PUIT("文件名不能包含下列字符: /  \\  :  *  ?  \"  <  >  |"));
								else
								{
									switch (funcdata.a.type)
									{
										case PFE_PathType_Dir:
										case PFE_PathType_File:
											if (MoveFileW(Utf8ToUnicode(funcdata.a.str).c_str(),Utf8ToUnicode(newpath=GetPreviousBeforeBackSlash(funcdata.a.str)+"\\"+str).c_str())==0)
												WarningMessageButton(SetSystemBeep_Error,PUIT("错误！"),PUIT("重命名失败！错误码："+llTOstr(GetLastError())));
											else DoNothing;
											break;
										case PFE_PathType_Volume:
											if (SetVolumeLabelW(Utf8ToUnicode(funcdata.a.str).c_str(),Utf8ToUnicode(str).c_str())==0)
												WarningMessageButton(SetSystemBeep_Error,PUIT("错误！"),PUIT("设置卷标失败！错误码："+llTOstr(GetLastError())));
											else
											{
												int pos=MainView_Find(funcdata.a);
												if (pos!=-1)
												{
													MainViewData &data=MainView_GetFuncData(pos);
													if (data.TT_Name!=NULL)
														data.TT_Name->SetText(data.path.name=str+"("+funcdata.a.str+")");
												}
											}
											break;
										default:
											WarningMessageButton(SetSystemBeep_Error,PUIT("错误！"),PUIT("无法对此目标重命名！"));
											break;
									}
									//update others...
									funcdata.b->Close();
								}
							}
						else funcdata.b->Close();
					}
				},{path,mbl});
			auto bu=new Button <decltype(tel)> (0,mbl,new PosizeEX_Fa6(1,2,60,20,40,10),PUIT("完成"),[](auto &tel)->void{tel->TriggerFunc();},tel);
			SetButtonDUCColor(bu);
			if (path.type==PFE_PathType_Volume)
				tel->SetText(path.name.substr(0,path.name.length()-path.str.length()-2)),
				tel->StartTextInput();
			else if (path.type==PFE_PathType_Dir)
				tel->SetText(GetLastAfterBackSlash(path.str)),
				tel->StartTextInput();
			else if (path.type==PFE_PathType_File)
				tel->SetText(GetLastAfterBackSlash(path.str)),
				tel->StartTextInput(Range(0,stringUTF8(GetLastAfterBackSlash(path.str)).length()-stringUTF8(GetAftername(path.str)).length()));
			else
				tel->SetText(path.name),
				tel->StartTextInput();
		}
		
		Operation_Rename(int)
		{ValidDo(OperationTargets(),OperationContext());}
		
		Operation_Rename() {}
};

class Operation_ClearRecycleBin:public BaseNodeOperation
{
	public:
		virtual string Name() const {return PUIT("清空回收站");}
		
		virtual int InvalidFlags(const OperationTargets &tar,const OperationContext &con) const
		{
			if (!tar.IsSingle())
				return InvalidFlag(InvalidBit_SingleMulti);
			if (NotInSet(PFE_PathType_RecycleBin,tar.Path().type,con.CurrentPath().type))
				return InvalidFlag(InvalidBit_PathType);
			if (NotInSet(con.From(),From_User,From_TopButton,From_MainViewMenu,From_MainViewBackground))
				return InvalidFlag(InvalidBit_From);
			return 0;
		}
		
		virtual void Func(const OperationTargets &tar,const OperationContext &con) const
		{
			OperationFuncCheckInvalid;
			if (tar.Path().type==PFE_PathType_RecycleBin)
				ClearRecycleBin(tar.Path());
			else if (con.CurrentPath().type==PFE_PathType_RecycleBin)
				ClearRecycleBin(con.CurrentPath());
		}
		
		Operation_ClearRecycleBin(int)
		{ValidDo(OperationTargets(),OperationContext());}
		
		Operation_ClearRecycleBin() {}
};

class Operation_RestoreRecycleBin:public BaseNodeOperation
{
	public:
		virtual string Name() const {return PUIT("还原");}
		
		virtual int InvalidFlags(const OperationTargets &tar,const OperationContext &con) const
		{
			if (!tar.IsSingle())
				return InvalidFlag(InvalidBit_SingleMulti);
			if (tar.Size()==0||con.CurrentPath().type!=PFE_PathType_RecycleBin)
				return InvalidFlag(InvalidBit_Path);
			if (NotInSet(con.From(),From_User,From_TopButton,From_MainViewMenu))
				return InvalidFlag(InvalidBit_From);
			return 0;
		}
		
		virtual void Func(const OperationTargets &tar,const OperationContext &con) const
		{
			OperationFuncCheckInvalid;
			RestoreRecycledFile(tar.Path());//? 
		}
		
		Operation_RestoreRecycleBin() {}
};

class Operation_NewDir:public BaseNodeOperation
{
	public:
		virtual string Name() const {return PUIT("新建文件夹");}
		
		virtual int InvalidFlags(const OperationTargets &tar,const OperationContext &con) const
		{
			if (!tar.IsSingle())
				return InvalidFlag(InvalidBit_SingleMulti);
			if (tar.Size()!=0)
				return InvalidFlag(InvalidBit_Path);
			if (NotInSet(con.From(),From_User,From_TopButton,From_MainViewBackground))
				return InvalidFlag(InvalidBit_From);
			if (NotInSet(tar.Path().type,PFE_PathType_Dir,PFE_PathType_Volume,PFE_PathType_Library))
				return InvalidFlag(InvalidBit_PathType);
			if (!CurrentMainViewIsNormal(con.MainView())||InThisSet(con.MainView(),CurrentMainView_BVT_RecycleBin,CurrentMainView_LVT_SearchResult))
				return InvalidFlag(InvalidBit_MainView);
			return 0;
		}
		
		virtual void Func(const OperationTargets &tar,const OperationContext &con) const
		{
			OperationFuncCheckInvalid;
			const PFE_Path &path=tar.Path();
			auto mbl=new MessageBoxLayer(0,PUIT("新建文件夹："),400,80);
			mbl->SetClickOutsideReaction(1);
			mbl->EnableShowTopAreaColor(1);
			mbl->SetBackgroundColor(DUC_MessageLayerBackground);
			mbl->SetTopAreaColor(DUC_MessageLayerTopAreaColor);
			auto tel=new TextEditLine <Doublet <PFE_Path,MessageBoxLayer*> > (0,mbl,new PosizeEX_Fa6(2,2,20,85,40,10),
				[](auto &funcdata,const stringUTF8 &strutf8,auto *tel,bool isenter)
				{
					if (isenter)
					{
						string name=strutf8.cppString(),newpath=funcdata.a.str+"\\"+name;
						if (name.empty())
							funcdata.b->Close();
						bool invalidflag=0;
						for (int i=0;i<name.length()&&!invalidflag;++i)
							for (int j=0;j<9&&!invalidflag;++j)
								if (name[i]==InvalidFileName[j])
									invalidflag=1;
						if (invalidflag)
							WarningMessageButton(SetSystemBeep_Error,PUIT("错误！"),PUIT("文件名不能包含下列字符: /  \\  :  *  ?  \"  <  >  |"));
						else
						{
							int x=GetFileAttributesW(Utf8ToUnicode(newpath).c_str());
							if (x==-1||!(x&FILE_ATTRIBUTE_DIRECTORY))
								if (x!=-1&&(x&FILE_ATTRIBUTE_DIRECTORY)||CreateDirectoryW(Utf8ToUnicode(newpath).c_str(),NULL))
								{
									SetMainViewContent(1e9,PFE_Path(PFE_PathType_Dir,newpath,name));//??
									funcdata.b->Close();
								}
								else WarningMessageButton(SetSystemBeep_Error,PUIT("错误！"),PUIT("新建文件夹失败！错误码："+llTOstr(GetLastError())));
							else WarningMessageButton(SetSystemBeep_Error,PUIT("错误！"),PUIT("文件已存在！"));
						}
					}
				},{path,mbl});
			auto bu=new Button <decltype(tel)> (0,mbl,new PosizeEX_Fa6(1,2,60,20,40,10),PUIT("完成"),[](auto &tel){tel->TriggerFunc();},tel);
			SetButtonDUCColor(bu);
			string xjwjj=PUIT("新建文件夹");
			for (int i=0;i<256;++i)
				if (!IsFileExsit(path.str+"\\"+(i==0?xjwjj:xjwjj+" ("+llTOstr(i)+")"),1))
				{
					tel->SetText(i==0?xjwjj:xjwjj+" ("+llTOstr(i)+")");
					break;
				}
			//tel->StartTextInput();??
		}
		
		Operation_NewDir(int)
		{ValidDo(OperationTargets(),OperationContext());}
		
		Operation_NewDir() {}
};

class Operation_NewItem:public BaseNodeOperation
{
		const string name,
					 fileName;
	public:
		virtual string Name() const {return name;}
		
		virtual int InvalidFlags(const OperationTargets &tar,const OperationContext &con) const
		{
			if (!tar.IsSingle())
				return InvalidFlag(InvalidBit_SingleMulti);
			if (tar.Size()!=0)
				return InvalidFlag(InvalidBit_Path);
			if (NotInSet(con.From(),From_User,From_TopButton,From_MainViewBackground))
				return InvalidFlag(InvalidBit_From);
			if (NotInSet(tar.Path().type,PFE_PathType_Dir,PFE_PathType_Volume,PFE_PathType_Library))
				return InvalidFlag(InvalidBit_PathType);
			if (!CurrentMainViewIsNormal(con.MainView())||InThisSet(con.MainView(),CurrentMainView_BVT_RecycleBin,CurrentMainView_LVT_SearchResult))
				return InvalidFlag(InvalidBit_MainView);
			return 0;
		}
		
		virtual void Func(const OperationTargets &tar,const OperationContext &con) const
		{
			OperationFuncCheckInvalid;
			const PFE_Path &path=tar.Path();
			auto mbl=new MessageBoxLayer(0,PUIT("新建文件："),400,80);
			mbl->SetClickOutsideReaction(1);
			mbl->EnableShowTopAreaColor(1);
			mbl->SetBackgroundColor(DUC_MessageLayerBackground);
			mbl->SetTopAreaColor(DUC_MessageLayerTopAreaColor);
			auto tel=new TextEditLine <Doublet <PFE_Path,MessageBoxLayer*> > (0,mbl,new PosizeEX_Fa6(2,2,20,85,40,10),
				[](auto &funcdata,const stringUTF8 &strutf8,auto *tel,bool isenter)
				{
					if (isenter)
					{
						string name=strutf8.cppString(),newpath=funcdata.a.str+"\\"+name;
						if (name.empty())
							funcdata.b->Close();
						bool invalidflag=0;
						for (int i=0;i<name.length()&&!invalidflag;++i)
							for (int j=0;j<9&&!invalidflag;++j)
								if (name[i]==InvalidFileName[j])
									invalidflag=1;
						if (invalidflag)
							WarningMessageButton(SetSystemBeep_Error,PUIT("错误！"),PUIT("文件名不能包含下列字符: /  \\  :  *  ?  \"  <  >  |"));
						else
						{
							int x=GetFileAttributesW(Utf8ToUnicode(newpath).c_str());
							HANDLE hfile;
							if (x==-1||(x&FILE_ATTRIBUTE_DIRECTORY))
								if (x!=-1&&!(x&FILE_ATTRIBUTE_DIRECTORY)||(hfile=CreateFileW(Utf8ToUnicode(newpath).c_str(),GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL))!=INVALID_HANDLE_VALUE)
								{
								    CloseHandle(hfile);
									SetMainViewContent(1e9,PFE_Path(PFE_PathType_File,newpath,name));//??
									funcdata.b->Close();
								}
								else WarningMessageButton(SetSystemBeep_Error,PUIT("错误！"),PUIT("新建文件失败！错误码："+llTOstr(GetLastError())));
							else WarningMessageButton(SetSystemBeep_Error,PUIT("错误！"),PUIT("文件已存在！"));
						}
					}
				},{path,mbl});
			auto bu=new Button <decltype(tel)> (0,mbl,new PosizeEX_Fa6(1,2,60,20,40,10),PUIT("完成"),[](auto &tel){tel->TriggerFunc();},tel);
			SetButtonDUCColor(bu);
			tel->SetText(fileName);
	//		tel->StartTextInput();
		}
		
		Operation_NewItem(int,const string &_name,const string &_filename):name(_name),fileName(_filename)
		{ValidDo(OperationTargets(),OperationContext());}
		
		Operation_NewItem(const string &_name,const string &_filename):name(_name),fileName(_filename) {}
};
Operation_NewItem Operation_New_Item(PUIT("文件"),PUIT("新文件")),
				  Operation_New_Txt(PUIT("文本文档"),PUIT("新建文本文档.txt")),
				  Operation_New_Cpp(PUIT("C++源代码文件"),PUIT("main.cpp")),
				  Operation_New_Docx(PUIT("Word文档"),PUIT("新建 Microsoft Word 文档.docx")),
				  Operation_New_Pptx(PUIT("PPT演示文稿"),PUIT("新建 Microsoft PowerPoint 演示文稿.pptx"));

class Operation_AddQuickList:public BaseNodeOperation
{
	public:
		virtual string Name() const {return PUIT("添加到快速访问");}
		
		virtual int InvalidFlags(const OperationTargets &tar,const OperationContext &con) const
		{
			if (!tar.IsSingle())
				return InvalidFlag(InvalidBit_SingleMulti);
			if (InThisSet(con.From(),From_RecentListMenu,From_QuickListMenu))
				return InvalidFlag(InvalidBit_From);
			if (InThisSet(tar.Path().type,PFE_PathType_VirtualDir,PFE_PathType_VirtualFile,PFE_PathType_None))
				return InvalidFlag(InvalidBit_PathType);
			if (InThisSet(con.MainView(),CurrentMainView_LVT_SearchResult)||tar.Size()!=0&&InThisSet(con.MainView(),CurrentMainView_BVT_RecycleBin))
				return InvalidFlag(InvalidBit_MainView);
			return 0;
		}
		
		virtual void Func(const OperationTargets &tar,const OperationContext &con) const
		{
			OperationFuncCheckInvalid;
			if (LVT_QuickList->Find(LVT_QuickListData(tar.Path(),0))==-1)//??
				SetLeftListItem(1e9,tar.Path(),0);
		}
		
		Operation_AddQuickList(int)
		{ValidDo(OperationTargets(),OperationContext());}
		
		Operation_AddQuickList() {}
};

class Operation_AddRecentList:public BaseNodeOperation
{
	public:
		virtual string Name() const {return PUIT("添加到近期访问");}
		
		virtual int InvalidFlags(const OperationTargets &tar,const OperationContext &con) const
		{
			if (!tar.IsSingle())
				return InvalidFlag(InvalidBit_SingleMulti);
			if (InThisSet(con.From(),From_RecentListMenu,From_QuickListMenu))
				return InvalidFlag(InvalidBit_From);
			if (InThisSet(tar.Path().type,PFE_PathType_VirtualDir,PFE_PathType_VirtualFile,PFE_PathType_None,PFE_PathType_MainPage))
				return InvalidFlag(InvalidBit_PathType);
			if (InThisSet(con.MainView(),CurrentMainView_LVT_SearchResult)||tar.Size()!=0&&InThisSet(con.MainView(),CurrentMainView_BVT_RecycleBin))
				return InvalidFlag(InvalidBit_MainView);
			return 0;
		}
		
		virtual void Func(const OperationTargets &tar,const OperationContext &con) const
		{
			OperationFuncCheckInvalid;
			if (LVT_RecentList->Find(LVT_QuickListData(tar.Path(),1))==-1)
				SetLeftListItem(1e9,tar.Path(),1);
		}
		
		Operation_AddRecentList(int)
		{ValidDo(OperationTargets(),OperationContext());}
		
		Operation_AddRecentList() {}
};

class Operation_MoveRemoveInLeftList:public BaseNodeOperation
{
	public:
		enum Mode
		{
			MoveUp,
			MoveDown,
			Remove,
			Clear
		}const mode;
		
		virtual string Name() const
		{
			switch (mode)
			{
				case MoveUp:	return PUIT("上移");
				case MoveDown:	return PUIT("下移");
				case Remove:	return PUIT("移出列表");
				case Clear:		return PUIT("清空列表");
			}
			return PUIT("错误:未定义操作!");
		}
		
		virtual int InvalidFlags(const OperationTargets &tar,const OperationContext &con) const
		{
			if (!tar.IsSingle())
				return InvalidFlag(InvalidBit_SingleMulti);
			if (NotInSet(con.From(),From_RecentListMenu,From_QuickListMenu))
				return InvalidFlag(InvalidBit_From);
			if (con.From()==From_QuickListMenu&&mode==Clear)
				return InvalidFlag(InvalidBit_User);
			return 0;
		}
		
		virtual void Func(const OperationTargets &tar,const OperationContext &con) const
		{
			OperationFuncCheckInvalid;
			const PFE_Path &path=tar.Path();
			if (con.From()==From_QuickListMenu)
			{
				int p=LVT_QuickList->Find(LVT_QuickListData(path,0));
				if (p!=-1)
					if (mode==MoveUp&&p>=2)	LVT_QuickList->SwapContent(p,p-1);
					else if (mode==MoveDown&&p!=LVT_QuickList->GetListCnt()-1) LVT_QuickList->SwapContent(p,p+1);
					else if (mode==Remove&&p!=0) LVT_QuickList->DeleteListContent(p);
			}
			else
				if (mode==Clear)
				{
					if (RecentListLRULimit!=0) LRU_RecentList.Clear();
					LVT_RecentList->ClearListContent();
				}
				else
				{
					int p=LVT_RecentList->Find(LVT_QuickListData(path,1));
					if (p!=-1)
						if (mode==MoveUp&&p>=2) LVT_RecentList->SwapContent(p,p-1);
						else if (mode==MoveDown&&p!=LVT_RecentList->GetListCnt()-1) LVT_RecentList->SwapContent(p,p+1);
						else if (mode==Remove&&p!=-1)
						{
							LVT_RecentList->DeleteListContent(p);
							LRU_RecentList.Erase(path);
						}
				}
		}
		
		Operation_MoveRemoveInLeftList(Mode _mode):mode(_mode) {}
};

class Operation_RunAt:public BaseNodeOperation
{
		const string name,
					 cmd,
					 argv;
		bool IsHiddenFunction;
	public:
		virtual string Name() const {return name;}
		
		virtual int InvalidFlags(const OperationTargets &tar,const OperationContext &con) const
		{
			if (!tar.IsSingle())
				return InvalidFlag(InvalidBit_SingleMulti);
			if (NotInSet(con.From(),From_User,From_TopButton,From_MainViewBackground))
				return InvalidFlag(InvalidBit_From);
			if (NotInSet(tar.Path().type,PFE_PathType_Dir,PFE_PathType_Volume))
				return InvalidFlag(InvalidBit_PathType);
			if (!CurrentMainViewIsNormal(con.MainView())||InThisSet(con.MainView(),CurrentMainView_LVT_SearchResult,CurrentMainView_BVT_RecycleBin))
				return InvalidFlag(InvalidBit_MainView);
			if (con.From()==From_MainViewBackground&&IsHiddenFunction&&!(SDL_GetModState()&KMOD_SHIFT))
				return InvalidFlag(InvalidBit_User);
			return 0;
		}
		
		virtual void Func(const OperationTargets &tar,const OperationContext &con) const
		{
			OperationFuncCheckInvalid;
			DD[0]<<"Run "<<cmd<<" at "<<tar.Path().str/*??*/<<endl;
			ShellExecuteW(0,L"open",Utf8ToUnicode(cmd).c_str(),Utf8ToUnicode(argv).c_str(),Utf8ToUnicode(tar.Path().str+"\\").c_str(),SW_SHOWNORMAL);
		}
		
		Operation_RunAt(int,const string &_name,const string &_cmd,const string &_argv="",bool ishidden=0)
		:name(_name),cmd(_cmd),argv(_argv),IsHiddenFunction(ishidden)
		{ValidDo(OperationTargets(),OperationContext());}
		
		Operation_RunAt(const string &_name,const string &_cmd,const string &_argv="",bool ishidden=0)
		:name(_name),cmd(_cmd),argv(_argv),IsHiddenFunction(ishidden) {}
};
Operation_RunAt Operation_RunAt_Cmd(PUIT("在此处打开命令行"),"cmd"),
				Operation_RunAt_PowerShell(PUIT("在此处打开Powershell"),"powershell","",1),
				Operation_RunAt_WSLBash(PUIT("在此处打开WSL Bash"),"bash","",1);

class Operation_SelectInWinExplorer:public BaseNodeOperation
{
	public:
		virtual string Name() const {return PUIT("在资源管理器中选中");}
		
		virtual int InvalidFlags(const OperationTargets &tar,const OperationContext &con) const
		{
			if (!tar.IsSingle())
				return InvalidFlag(InvalidBit_SingleMulti);
			if (NotInSet(tar.Path().type,PFE_PathType_File,PFE_PathType_Dir,PFE_PathType_Volume,PFE_PathType_MainPage))
				return InvalidFlag(InvalidBit_PathType);
			if (tar.IsGlobal()&&con.MainView()==CurrentMainView_BVT_RecycleBin&&!(RecycleBinAllowedOperation&RecycleBinAllowedOperation_SelectInWinExplorer))
				return InvalidFlag(InvalidBit_MainView);
			return 0;
		}
		
		virtual void Func(const OperationTargets &tar,const OperationContext &con) const
		{
			OperationFuncCheckInvalid;
			SelectInWinExplorer(tar.Path().str);
		}
		
		Operation_SelectInWinExplorer(int)
		{ValidDo(OperationTargets(),OperationContext());}
		
		Operation_SelectInWinExplorer() {}
};

class Operation_ShellContextMenu:public BaseNodeOperation
{
	public:
		struct ShellContextMenuStructData
		{
			vector <wstring> paths;
			HWND h;
			POINT pt;
			bool IsBackground;
		};
			
		virtual string Name() const {return PUIT("资源管理器选项");}
		
		virtual int InvalidFlags(const OperationTargets &tar,const OperationContext &con) const
		{
			if (!tar.IsSingle())
				return InvalidFlag(InvalidBit_SingleMulti);
			if (NotInSet(con.From(),From_User,From_TopButton,From_MainViewMenu))
				return InvalidFlag(InvalidBit_From);
			if (NotInSet(tar.Path().type,PFE_PathType_File,PFE_PathType_Dir))
				return InvalidFlag(InvalidBit_PathType);
			if (tar.IsGlobal()&&con.MainView()==CurrentMainView_BVT_RecycleBin&&!(RecycleBinAllowedOperation&RecycleBinAllowedOperation_SelectInWinExplorer))
				return InvalidFlag(InvalidBit_MainView);
			return 0;
		}
		
		virtual void Func(const OperationTargets &tar,const OperationContext &con) const
		{
			OperationFuncCheckInvalid;
			ShellContextMenuStructData *data=new ShellContextMenuStructData;
			PUI_Window *win=CurrentWindow;
			tar.DoForAllT<ShellContextMenuStructData*>([](ShellContextMenuStructData *data,const PFE_Path &path)
			{
				data->paths.push_back(Utf8ToUnicode(path.str));
			},data);
			data->h=win->GetWindowsHWND();
			data->IsBackground=0;//??
			Point pt=win->NowPos();
			data->pt.x=pt.x;
			data->pt.y=pt.y;
		//	SDL_DetachThread(SDL_CreateThread([](void *_data)->int
		//	{
		//		auto *data=(ShellContextMenuStructData*)_data;
				PAL_Platform::ShellContextMenu scm;
				scm.ShowContextMenu(data->paths,data->h,&data->pt);
				delete data;
		//		return 0;
		//	},"Thread_OpenShellContextMenu",data));
		}
		
		Operation_ShellContextMenu(int)
		{ValidDo(OperationTargets(),OperationContext());}
		
		Operation_ShellContextMenu() {}
};

class Operation_Property:public BaseNodeOperation
{
	public:
		virtual string Name() const {return PUIT("属性");}
		
		virtual int InvalidFlags(const OperationTargets &tar,const OperationContext &con) const
		{
			if (!tar.IsSingle())
				return InvalidFlag(InvalidBit_SingleMulti);
			if (NotInSet(tar.Path().type,PFE_PathType_File,PFE_PathType_Dir,PFE_PathType_Volume,PFE_PathType_MainPage,PFE_PathType_VirtualDir,PFE_PathType_VirtualFile))
				return InvalidFlag(InvalidBit_PathType);
			if (con.From()==From_MainViewBackground&&InThisSet(con.MainView(),CurrentMainView_BVT_RecycleBin,CurrentMainView_LVT_SearchResult))
				return InvalidFlag(InvalidBit_MainView);
			if (tar.IsGlobal()&&con.MainView()==CurrentMainView_BVT_RecycleBin&&!(RecycleBinAllowedOperation&RecycleBinAllowedOperation_WinProperty))
				return InvalidFlag(InvalidBit_MainView);
			return 0;
		}
		
		virtual void Func(const OperationTargets &tar,const OperationContext &con) const
		{
			OperationFuncCheckInvalid;
			if (InThisSet(tar.Path().type,PFE_PathType_VirtualDir,PFE_PathType_VirtualFile))
				ShowVirtualFileProperty(tar.Path());
			else ShowPropertiesOfExplorer(tar.Path().str);
		}
		
		Operation_Property(int)
		{ValidDo(OperationTargets(),OperationContext());}
		
		Operation_Property() {}
};
//End of FileNodeOperation

//TopArea area

Widgets::WidgetType UserWidget_OperationButton=Widgets::GetNewWidgetTypeID("OperationButton");
class OperationButton:public BaseButton
{
	protected:
		BaseNodeOperation *operation=nullptr;
		string Text;
		int TextDividePos=0,
			LastW=0;
		int textMode=0;
		bool ValidState=0; 
		RGBA ButtonColor[2][3],
			 TextColor=ThemeColorMT[0];

		virtual void TriggerButtonFunction(bool isSubButton)
		{
			if (!isSubButton&&operation!=nullptr)
			{
				OperationTargets tar;
				OperationContext con;
				ValidState=operation->Valid(tar,con);
				if (ValidState)
					operation->Func(tar,con);
			}
		}
		
		virtual void Show(Posize &lmt)
		{
			Win->RenderFillRect(lmt,ThemeColor(ButtonColor[ValidState][stat]));
			if (gPS.w!=LastW)
			{
				if (GetStringWidth(Text)<=gPS.w)
					TextDividePos=0;
				else
				{
					stringUTF8 strutf8(Text);
					TextDividePos=strutf8.substr(0,strutf8.length()/2).length();//??
				}
				LastW=gPS.w;
			}
			if (TextDividePos!=0)
			{
				Win->RenderDrawText(Text.substr(0,TextDividePos),Posize(gPS.x,gPS.midY()-15-(gPS.h>50?20:10),gPS.w,30),lmt,textMode,ThemeColor(TextColor));
				Win->RenderDrawText(Text.substr(TextDividePos,Text.length()-TextDividePos),Posize(gPS.x,gPS.midY()-15+(gPS.h>50?20:10),gPS.w,30),lmt,textMode,ThemeColor(TextColor));
			}
			else Win->RenderDrawText(Text,gPS,lmt,textMode,ThemeColor(TextColor));
			Win->Debug_DisplayBorder(gPS);
		}
		
		void SetDUCColor()//DUC should be inited before!
		{
			for (int i=0;i<=2;++i)
				ButtonColor[0][i]=DUC_InvalidButtonColor[i];
			for (int i=0;i<=2;++i)
				ButtonColor[1][i]=DUC_ButtonColor[i];
		}
	
	public:
		static set <OperationButton*> AllOperationButton;
		
		void Update()
		{
//			int x=operation->InvalidFlags(OperationTargets(),OperationContext());
//			if (x)
//				DD[3]<<operation->Name()<<" invalid "<<x<<" "<<OperationTargets().Path().str<<" "<<MainViewItemSelectCount()<<endl;
//			bool flag=!x;
			bool flag=operation->Valid(OperationTargets(),OperationContext());
			if (ValidState!=flag)
			{
				Win->SetPresentArea(CoverLmt);
				ValidState=flag;
			}
		}
		
		~OperationButton()
		{
			AllOperationButton.erase(this);
		}
		
		OperationButton(const WidgetID &_ID,Widgets *_fa,PosizeEX *psex,BaseNodeOperation *ope,const string &text="")
		:BaseButton(_ID,UserWidget_OperationButton,_fa,psex),operation(ope)
		{
			AllOperationButton.insert(this);
			if (text=="")
				Text=operation->Name();
			else Text=text;
			SetDUCColor();
		}
};
set <OperationButton*> OperationButton::AllOperationButton;
bool NeedUpdateOperationButton=0;

void UpdateAllOperationButton()
{
	for (auto vp:OperationButton::AllOperationButton)
		vp->Update();
	NeedUpdateOperationButton=0;
}

inline void SetNeedUpdateOperationButton()
{
	NeedUpdateOperationButton=1;
	UpdateAllOperationButton();//efficiency??
}
//End of TopArea

//SettingPage Area
enum
{
	SettingLevel_Easy=0,
	SettingLevel_Normal,
	SettingLevel_Advanced,
	SettingLevel_Debug
};

void SetBackgroundPic_Surface(SDL_Surface *sur,const string &path,int from)
{
	if (sur==nullptr)
	{
		PFE_Cfg("BackgroundPicture")="";
		PB_Background->SetPicture(SharedTexturePtr(nullptr),BackgroundPictureBoxMode);
		MainWindow->SetBackgroundColor(RGBA_WHITE);
		HaveBackgroundPic=0;
		SetUIWidgetsOpacity(0);
		if (from==2)
			(new MessageBoxButtonI(0,PUIT("错误"),PUIT("目标路径文件不存在或不支持！")))->AddButton(PUIT("确定"),NULL,0);
	}
	else
	{
		PFE_Cfg("BackgroundPicture")=path;
		PB_Background->SetPicture(SharedTexturePtr(MainWindow->CreateTextureFromSurfaceAndDelete(sur)),BackgroundPictureBoxMode);
		MainWindow->SetBackgroundColor(RGBA_NONE);
		HaveBackgroundPic=1;
		SetUIWidgetsOpacity(WidgetsOpacityPercent);
	}
}

bool SetBackgroundPic(const string &path,int from)//from0:User sync 1:User async 2:Setting
{
	if (DeleteEndBlank(path).empty())
		SetBackgroundPic_Surface(nullptr,"",0);
	else if (from==1)
		PAL_Thread <int,string> ([](string &path)->int
		{
			SDL_Surface *sur=IMG_Load(path.c_str());
			PUI_SendFunctionEvent<Doublet<SDL_Surface*,string> >([](Doublet<SDL_Surface*,string> &data)
			{
				SetBackgroundPic_Surface(data.a,data.b,1);
			},{sur,path});
			return 0;
		},path).Detach();
	else SetBackgroundPic_Surface(IMG_Load(path.c_str()),path,from);
	return HaveBackgroundPic;
}

int LastSettingLevel=0;
void OpenSettingPageInLayer(Widgets *lay,int SettingLevel=LastSettingLevel)//if lay==nullptr,means close
{
	if (lay==nullptr)
	{
		DelayDeleteToNULL(Lay_SettingPage);
		return;
	}
	if (Lay_SettingPage!=nullptr)
		Lay_SettingPage->DelayDelete();
	LastSettingLevel=SettingLevel;
	Lay_SettingPage=new Layer(0,lay,new PosizeEX_Fa6(2,2,0,0,0,0));
	LargeLayerWithScrollBar *LarLay_Setting=new LargeLayerWithScrollBar(0,Lay_SettingPage,new PosizeEX_Fa6(2,2,0,0,0,0),ZERO_POSIZE);
	int EachSettingInterval=5,EachSettingMinWidth=600;
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,0,0,80));
		fa->SetLayerColor(DUC_SettingItemBackground);
		TinyText *tt=new TinyText(0,fa,{50,30,300,40},PUIT("设置"),-1);
		tt->SetFontSize(18);
		tt->SetTextColor(ThemeColorM[7]);
		DropDownButtonI *ddb=new DropDownButtonI(0,fa,new PosizeEX_Fa6(1,3,140,160,40,30),"",
			[](int &level,int)
			{
				if (Lay_SettingPage!=nullptr)
					OpenSettingPageInLayer(Lay_SettingPage->GetFa(),level);
			});
		ddb->SetBackgroundColor(DUC_ClearBackground0);
		ddb->PushbackChoiceData(PUIT("简易"),SettingLevel_Easy);
		ddb->PushbackChoiceData(PUIT("常规"),SettingLevel_Normal);
		ddb->PushbackChoiceData(PUIT("高级"),SettingLevel_Advanced);
		ddb->PushbackChoiceData(PUIT("调试"),SettingLevel_Debug);
		ddb->SetSelectChoice(SettingLevel,0);
	}
	
	if (SettingLevel>=SettingLevel_Normal)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		TinyText *tt=new TinyText(0,fa,{100,0,300,30},PUIT("关闭硬件加速(重启生效)"),-1);
		SwitchButtonI *sb=new SwitchButtonI(0,fa,new PosizeEX_Fa6(1,2,40,160,7,7),PFE_Cfg("DisableRendererAccelerate")=="1",
			[](int&,bool onoff)
			{
				PFE_Cfg("DisableRendererAccelerate")=onoff?"1":"0";
			},0);
	}
	
	if (SettingLevel>=SettingLevel_Advanced)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		TinyText *tt=new TinyText(0,fa,{100,0,300,30},PUIT("关闭渲染器线性插值"),-1);
		SwitchButtonI *sb=new SwitchButtonI(0,fa,new PosizeEX_Fa6(1,2,40,160,7,7),PFE_Cfg("DisableRenderLinearScale")=="1",
			[](int&,bool onoff)
			{
				PFE_Cfg("DisableRenderLinearScale")=onoff?"1":"0";
				SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,onoff?"nearest":"linear");
			},0);
	}
	
	if (SettingLevel>=SettingLevel_Advanced)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		TinyText *tt=new TinyText(0,fa,{100,0,300,30},PUIT("优先选择渲染器(重启生效)"),-1);
		DropDownButtonI *ddb=new DropDownButtonI(0,fa,new PosizeEX_Fa6(1,3,140,160,0,30),"",
			[](int &rendererIndex,int pos)
			{
				PFE_Cfg("PreferredRenderer")=llTOstr(rendererIndex);
			});
		ddb->SetBackgroundColor(DUC_ClearBackground0);
		for (int i=0;i<PUI_PreferredRenderer_End;++i)
			ddb->PushbackChoiceData(PUI_PreferredRendererName[i],i);
		ddb->SetSelectChoice(PFE_Cfg("PreferredRenderer")==""?0:strTOll(PFE_Cfg("PreferredRenderer")),0);
	}
	
	if (SettingLevel>=SettingLevel_Easy)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		TinyText *tt=new TinyText(0,fa,{100,0,300,30},PUIT("记住上次窗口大小和位置"),-1);
		SwitchButtonI *sb=new SwitchButtonI(0,fa,new PosizeEX_Fa6(1,2,40,160,7,7),PFE_Cfg("EnableRememberLastWindowPosize")=="1",
			[](int&,bool onoff)
			{
				PFE_Cfg("EnableRememberLastWindowPosize")=onoff?"1":"0";
			},0);
	}
	
	if (SettingLevel>=SettingLevel_Easy)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		TinyText *tt=new TinyText(0,fa,{100,0,300,30},PUIT("自动选择刚才选中的文件(夹)"),-1);
		SwitchButtonI *sb=new SwitchButtonI(0,fa,new PosizeEX_Fa6(1,2,40,160,7,7),EnableAutoSelectLastPath,
			[](int&,bool onoff)
			{
				EnableAutoSelectLastPath=onoff;
				PFE_Cfg("EnableAutoSelectLastPath")=onoff?"1":"0";
			},0);
	}
	
	if (SettingLevel>=SettingLevel_Easy)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		TinyText *tt=new TinyText(0,fa,{100,0,300,30},PUIT("显示隐藏文件(夹)"),-1);
		SwitchButtonI *sb=new SwitchButtonI(0,fa,new PosizeEX_Fa6(1,2,40,160,7,7),ShowHiddenFile,
			[](int&,bool onoff)
			{
				ShowHiddenFile=onoff;
				PFE_Cfg("ShowHiddenFile")=onoff?"1":"0";
			},0);
	}
	
	if (SettingLevel>=SettingLevel_Normal)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		TinyText *tt=new TinyText(0,fa,{100,0,300,30},PUIT("在右键菜单显示永久删除"),-1);
		SwitchButtonI *sb=new SwitchButtonI(0,fa,new PosizeEX_Fa6(1,2,40,160,7,7),ShowRealDeleteInRigthClickMenu,
			[](int&,bool onoff)
			{
				ShowRealDeleteInRigthClickMenu=onoff;
				PFE_Cfg("ShowRealDeleteInRigthClickMenu")=ShowRealDeleteInRigthClickMenu?"1":"0";
			},0);
	}
	
	if (SettingLevel>=SettingLevel_Easy)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		TinyText *tt=new TinyText(0,fa,{100,0,300,30},PUIT("显示文件后缀名"),-1);
		SwitchButtonI *sb=new SwitchButtonI(0,fa,new PosizeEX_Fa6(1,2,40,160,7,7),ShowAfternameInName,
			[](int&,bool onoff)
			{
				ShowAfternameInName=onoff;
				PFE_Cfg("ShowAfternameInName")=onoff?"1":"0";
			},0);
	}
	
	if (SettingLevel>=SettingLevel_Normal)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		TinyText *tt=new TinyText(0,fa,{100,0,300,30},PUIT("同步系统剪切板"),-1);
		SwitchButtonI *sb=new SwitchButtonI(0,fa,new PosizeEX_Fa6(1,2,40,160,7,7),SynchronizeWithSystemClipboard,
			[](int&,bool onoff)
			{
				SynchronizeWithSystemClipboard=onoff;
				PFE_Cfg("SynchronizeWithSystemClipboard")=onoff?"1":"0";
			},0);
	}
	
	if (SettingLevel>=SettingLevel_Easy)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		TinyText *tt=new TinyText(0,fa,{100,0,300,30},PUIT("文件操作完成发出提示音"),-1);
		SwitchButtonI *sb=new SwitchButtonI(0,fa,new PosizeEX_Fa6(1,2,40,160,7,7),RingWhenAllFileOperationIsCompleted,
			[](int&,bool onoff)
			{
				RingWhenAllFileOperationIsCompleted=onoff;
				PFE_Cfg("RingWhenAllFileOperationIsCompleted")=onoff?"1":"0";
			},0);
	}
	
	if (SettingLevel>=SettingLevel_Debug)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		TinyText *tt=new TinyText(0,fa,{100,0,300,30},PUIT("近期列表颜色渐变"),-1);
		SwitchButtonI *sb=new SwitchButtonI(0,fa,new PosizeEX_Fa6(1,2,40,160,7,7),PFE_Cfg("EnableRecentListGradualColor")=="1",
			[](int&,bool onoff)
			{
				PFE_Cfg("EnableRecentListGradualColor")=onoff?"1":"0";
				if (RecentListLRULimit!=0)//??
				{
					const auto &vec=LRU_RecentList.GetLinkedData();
					for (int i=0;i<vec.size();++i)
					{
						int p=LVT_RecentList->Find(LVT_QuickListData(vec[i].a,1));
						if (p!=-1)
						{
							LVT_QuickListData &data=LVT_RecentList->GetFuncData(p);
							data.TT_Name->SetTextColor(onoff?RGBA_WHITE<<RGBA(83,97,225,255-i*20):ThemeColorMT[0]);
							data.NumCode=i+1;
						};
					}
				}
			},0);
	}
	
	if (SettingLevel>=SettingLevel_Advanced)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		TinyText *tt=new TinyText(0,fa,{100,0,300,30},PUIT("近期列表大小"),-1);
		DropDownButtonI *ddb=new DropDownButtonI(0,fa,new PosizeEX_Fa6(1,3,140,160,0,30),RecentListLRULimit==0?PUIT("关闭"):llTOstr(RecentListLRULimit),
			[](int &limit,int pos)
			{
				PFE_Cfg("RecentListLRULimit")=llTOstr(RecentListLRULimit=limit);
				LRU_RecentList.SetSizeLimit(limit);
			});
		ddb->SetBackgroundColor(DUC_ClearBackground0);
		ddb->PushbackChoiceData(PUIT("关闭"),0);
		for (int i=1;i<=20;++i)
			ddb->PushbackChoiceData(llTOstr(i),i);
		for (int i=30;i<=100;i+=10)
			ddb->PushbackChoiceData(llTOstr(i),i);
	}
	
	if (SettingLevel>=SettingLevel_Easy)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		TinyText *tt=new TinyText(0,fa,{100,0,300,30},PUIT("记住上次的历史路径"),-1);
		DropDownButtonI *ddb=new DropDownButtonI(0,fa,new PosizeEX_Fa6(1,3,140,160,0,30),"",
			[](int &mode,int pos)
			{
				PFE_Cfg("EnableRememberPathHistory")=llTOstr(mode);
			});
		ddb->SetBackgroundColor(DUC_ClearBackground0);
		ddb->PushbackChoiceData(PUIT("关闭"),0);
		ddb->PushbackChoiceData(PUIT("开启"),1);
		ddb->PushbackChoiceData(PUIT("每次询问"),2);
		ddb->SetSelectChoice(PFE_Cfg("EnableRememberPathHistory")=="2"?2:PFE_Cfg("EnableRememberPathHistory")=="1",0);
	}
	
	if (SettingLevel>=SettingLevel_Normal)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		TinyText *tt=new TinyText(0,fa,{100,0,300,30},PUIT("背景图片载入模式"),-1);
		DropDownButtonI *ddb=new DropDownButtonI(0,fa,new PosizeEX_Fa6(1,3,140,160,0,30),"",
			[](int &mode,int pos)
			{
				PFE_Cfg("InitBackgroundPictureLoadMode")=llTOstr(mode);
			});
		ddb->SetBackgroundColor(DUC_ClearBackground0);
		ddb->PushbackChoiceData(PUIT("同步加载"),0);
		ddb->PushbackChoiceData(PUIT("异步加载(启动加速)"),1);
//		ddb->PushbackChoiceData(PUIT("缓存模式"),2);
//		ddb->PushbackChoiceData(PUIT("混合模式"),3);
		ddb->SetSelectChoice(strISull(PFE_Cfg("InitBackgroundPictureLoadMode"))?strTOll(PFE_Cfg("InitBackgroundPictureLoadMode")):InitBackgroundPictureLoadMode,0);
	}
	
	if (SettingLevel>=SettingLevel_Advanced)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		TinyText *tt=new TinyText(0,fa,{100,0,300,30},PUIT("切换为图片文件夹的检测百分比"),-1);
		DropDownButtonI *ddb=new DropDownButtonI(0,fa,new PosizeEX_Fa6(1,3,140,160,0,30),SwitchToPictureViewPercent==200?PUIT("永不"):llTOstr(SwitchToPictureViewPercent)+"%",
			[](int &per,int pos)
			{
				PFE_Cfg("SwitchToPictureViewPercent")=llTOstr(SwitchToPictureViewPercent=per);
			});
		ddb->SetBackgroundColor(DUC_ClearBackground0);
		for (int i=30;i<=100;i+=5)
			ddb->PushbackChoiceData(llTOstr(i)+"%",i);
		ddb->PushbackChoiceData(PUIT("关闭"),200);
	}
	
	if (SettingLevel>=SettingLevel_Easy)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		TinyText *tt=new TinyText(0,fa,{100,0,300,30},PUIT("在标题栏显示当前路径"),-1);
		DropDownButtonI *ddb=new DropDownButtonI(0,fa,new PosizeEX_Fa6(1,3,140,160,0,30),"",
			[](int &mode,int pos)
			{
				PFE_Cfg("ShowFullPathInWindowTitle")=llTOstr(ShowFullPathInWindowTitle=mode);
				if (ShowFullPathInWindowTitle==0)
					MainWindow->SetWindowTitle(ProgramNameVersion);
				else if (ShowFullPathInWindowTitle==1||CurrentPath.str=="")
					MainWindow->SetWindowTitle(ProgramNameVersion+" | "+CurrentPath.name);
				else MainWindow->SetWindowTitle(ProgramNameVersion+" | "+CurrentPath.str);
			});
		ddb->SetBackgroundColor(DUC_ClearBackground0);
		ddb->PushbackChoiceData(PUIT("关闭"),0);
		ddb->PushbackChoiceData(PUIT("仅名称"),1);
		ddb->PushbackChoiceData(PUIT("完整路径"),2);
		ddb->SetSelectChoice(ShowFullPathInWindowTitle,0);
	}
	
	if (SettingLevel>=SettingLevel_Easy)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		TinyText *tt=new TinyText(0,fa,{100,0,300,30},PUIT("启用标签页"),-1);
		DropDownButtonI *ddb=new DropDownButtonI(0,fa,new PosizeEX_Fa6(1,3,140,160,0,30),"",
			[](int &mode,int pos)
			{
				SwitchTabMode(mode);
			});
		ddb->SetBackgroundColor(DUC_ClearBackground0);
		ddb->PushbackChoiceData(PUIT("关闭"),TabMode_None);
		ddb->PushbackChoiceData(PUIT("顶部"),TabMode_Top);
		ddb->PushbackChoiceData(PUIT("路径栏"),TabMode_Addr);
		ddb->SetSelectChoice(CurrentTabMode,0);
	}
	
	if (SettingLevel>=SettingLevel_Easy)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		TinyText *tt=new TinyText(0,fa,{100,0,300,30},PUIT("近期文件高亮模式"),-1);
		DropDownButtonI *ddb=new DropDownButtonI(0,fa,new PosizeEX_Fa6(1,3,140,160,0,30),"",
			[](int &mode,int pos)
			{
				EnableRecentFileHighLight=mode;
				PFE_Cfg("EnableRecentFileHighLight")=llTOstr(mode);
			});
		ddb->SetBackgroundColor(DUC_ClearBackground0);
		ddb->PushbackChoiceData(PUIT("关闭"),0);
		ddb->PushbackChoiceData(PUIT("仅名字"),1);
		ddb->PushbackChoiceData(PUIT("仅边框"),2);
		ddb->PushbackChoiceData(PUIT("默认(名字+边框)"),3);
		ddb->SetSelectChoice(EnableRecentFileHighLight,0);
	}
	
	if (SettingLevel>=SettingLevel_Normal)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		TinyText *tt=new TinyText(0,fa,{100,0,300,30},PUIT("图标模式"),-1);
		DropDownButtonI *ddb=new DropDownButtonI(0,fa,new PosizeEX_Fa6(1,3,140,160,0,30),"",
			[](int &mode,int pos)
			{
				SwitchGetIconMode(mode);
				PFE_Cfg("CurrentIconGetMode")=llTOstr(CurrentIconGetMode);
			});
		ddb->SetBackgroundColor(DUC_ClearBackground0);
		ddb->PushbackChoiceData(PUIT("关闭"),IconGetMode_None);
		ddb->PushbackChoiceData(PUIT("默认模式"),IconGetMode_Default);
		ddb->PushbackChoiceData(PUIT("系统模式"),IconGetMode_PureWinAPI);
		ddb->PushbackChoiceData(PUIT("混合模式"),IconGetMode_AutoMixed);
		ddb->SetSelectChoice(CurrentIconGetMode,0);
	}
	
	if (SettingLevel>=SettingLevel_Advanced)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		TinyText *tt=new TinyText(0,fa,{100,0,300,30},PUIT("关闭默认模式图片文件缩略图"),-1);
		SwitchButtonI *sb=new SwitchButtonI(0,fa,new PosizeEX_Fa6(1,2,40,160,7,7),DisablePictureFileThumbnail,
			[](int&,bool onoff)
			{
				DisablePictureFileThumbnail=onoff;
				PFE_Cfg("DisablePictureFileThumbnail")=onoff?"1":"0";
			},0);
	}
	
	if (SettingLevel>=SettingLevel_Advanced)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		TinyText *tt=new TinyText(0,fa,{100,0,300,30},PUIT("关闭默认模式可执行文件图标获取"),-1);
		SwitchButtonI *sb=new SwitchButtonI(0,fa,new PosizeEX_Fa6(1,2,40,160,7,7),DisableExtractEXEIcon,
			[](int&,bool onoff)
			{
				DisableExtractEXEIcon=onoff;
				PFE_Cfg("DisableExtractEXEIcon")=onoff?"1":"0";
			},0);
	}
	
//	if (SettingLevel>=SettingLevel_Advanced)
//	{
//		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
//		fa->SetLayerColor(DUC_SettingItemBackground);
//		TinyText *tt=new TinyText(0,fa,{100,0,300,30},PUIT("启用默认模式文件夹内容预览"),-1);
//		SwitchButtonI *sb=new SwitchButtonI(0,fa,new PosizeEX_Fa6(1,2,40,160,7,7),EnableDirWithPreview,
//			[](int&,bool onoff)
//			{
//				EnableDirWithPreview=onoff;
//				PFE_Cfg("EnableDirWithPreview")=onoff?"1":"0";
//			},0);
//	}
	
	if (SettingLevel>=SettingLevel_Advanced)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		TinyText *tt=new TinyText(0,fa,{100,0,300,30},PUIT("混合图标模式下使用默认缩略图机制"),-1);
		SwitchButtonI *sb=new SwitchButtonI(0,fa,new PosizeEX_Fa6(1,2,40,160,7,7),IconModeAutoMixedUseDefaultThumb,
			[](int&,bool onoff)
			{
				IconModeAutoMixedUseDefaultThumb=onoff;
				PFE_Cfg("IconModeAutoMixedUseDefaultThumb")=onoff?"1":"0";
			},0);
	}
	
	if (SettingLevel>=SettingLevel_Advanced)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		TinyText *tt=new TinyText(0,fa,{100,0,300,30},PUIT("默认缩略图机制下空闲时自动加载"),-1);
		SwitchButtonI *sb=new SwitchButtonI(0,fa,new PosizeEX_Fa6(1,2,40,160,7,7),EnablePreloadNearbyThumbWhenFree,
			[](int&,bool onoff)
			{
				EnablePreloadNearbyThumbWhenFree=onoff;
				PFE_Cfg("EnablePreloadNearbyThumbWhenFree")=onoff?"1":"0";
			},0);
	}
	
	if (SettingLevel>=SettingLevel_Advanced)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		TinyText *tt=new TinyText(0,fa,{100,0,300,30},PUIT("近期图标缓存大小"),-1);
		DropDownButtonI *ddb=new DropDownButtonI(0,fa,new PosizeEX_Fa6(1,3,140,160,0,30),MemCachedItemIconSize==0?PUIT("关闭"):llTOstr(MemCachedItemIconSize)+(MemCachedItemIconSize==MemCachedItemIconDefaultSize?PUIT(" (默认)"):""),
			[](int &limit,int pos)
			{
				SetMemCachedItemIconSize(limit);
			});
		ddb->SetBackgroundColor(DUC_ClearBackground0);
		ddb->PushbackChoiceData(PUIT("关闭"),0);
		for (int i=16;i<=8192;i*=2)
			ddb->PushbackChoiceData(llTOstr(i)+(i==MemCachedItemIconDefaultSize?PUIT(" (默认)"):""),i);
	}
	
	if (SettingLevel>=SettingLevel_Easy)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		TinyText *tt=new TinyText(0,fa,{100,0,300,30},PUIT("回收站相关"),-1);
		ButtonI *bu_open=new ButtonI(0,fa,new PosizeEX_Fa6(1,3,120,160,0,30),PUIT("打开回收站"),
			[](int&)
			{
				SetCurrentPath(PFE_Path_RecycleBin,SetCurrentPathFrom_User);
			},0);
		SetButtonDUCColor(bu_open);
		ButtonI *bu_clear=new ButtonI(0,fa,new PosizeEX_Fa6(1,3,120,290,0,30),PUIT("清空回收站"),
			[](int&)
			{
				ClearRecycleBin(PFE_Path_RecycleBin);
			},0);
		SetButtonDUCColor(bu_clear);
		if (SettingLevel>=SettingLevel_Normal)
		{
			//...
		}
		if (SettingLevel>=SettingLevel_Advanced)
		{
			ButtonI *bu_advancedSetting=new ButtonI(0,fa,new PosizeEX_Fa6(1,3,120,420,0,30),PUIT("回收站高级设置"),
				[](int&)
				{
					auto mbl=new MessageBoxLayer(0,PUIT("回收站高级设置"),440,340);
					mbl->EnableShowTopAreaColor(1);
					auto bu=new Button <Doublet <MessageBoxLayer*,int> > (0,mbl,new PosizeEX_Fa6(1,1,60,30,30,30),PUIT("应用"),
						[](Doublet <MessageBoxLayer*,int> &funcdata)
						{
							PFE_Cfg("RecycleBinAllowedOperation")=RecycleBinAllowedOperation=funcdata.b;
							funcdata.a->Close();
						},{mbl,RecycleBinAllowedOperation});
					new TinyText(0,mbl,new PosizeEX_Fa6(3,3,30,300,40,30),PUIT("在回收站允许的操作:"),-1);
					#define AOIRB(y,text,val)																																		\
						new TinyText(0,mbl,new PosizeEX_Fa6(3,3,60,300,y,20),text,-1);																								\
						new SwitchButton<Doublet<Button <Doublet <MessageBoxLayer*,int> >*,int> >(0,mbl,new PosizeEX_Fa6(1,3,40,30,y+2,16),bool(RecycleBinAllowedOperation&(val)),	\
							[](Doublet <Button <Doublet <MessageBoxLayer*,int> >*,int> &data,bool onoff)																			\
							{																																						\
								if (onoff)																																			\
									data.a->GetFuncData().b|=data.b;																												\
								else data.a->GetFuncData().b&=~data.b;																												\
							},{bu,val});
					AOIRB(70,PUIT("打开文件"),RecycleBinAllowedOperation_OpenFile);
					AOIRB(90,PUIT("打开文件夹"),RecycleBinAllowedOperation_OpenDir);
					AOIRB(110,PUIT("复制"),RecycleBinAllowedOperation_Copy);
					AOIRB(130,PUIT("移动"),RecycleBinAllowedOperation_Move);
					AOIRB(150,PUIT("删除"),RecycleBinAllowedOperation_Delete);
					AOIRB(170,PUIT("重命名"),RecycleBinAllowedOperation_Rename);
					AOIRB(190,PUIT("在资源管理器选中"),RecycleBinAllowedOperation_SelectInWinExplorer);
					AOIRB(210,PUIT("资源管理器选项"),RecycleBinAllowedOperation_ShellContextMenu);
					AOIRB(230,PUIT("资源管理器属性"),RecycleBinAllowedOperation_WinProperty);
					AOIRB(250,PUIT("属性"),RecycleBinAllowedOperation_Property);
					#undef AOIRB
				},0);
			SetButtonDUCColor(bu_advancedSetting);
		}
	}
	
	if (SettingLevel>=SettingLevel_Advanced)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		TinyText *tt=new TinyText(0,fa,{100,0,300,30},PUIT("可识别的图片文件后缀名: "),-1);
		TextEditLineI *tel=new TextEditLineI(0,fa,new PosizeEX_Fa6(2,2,300,160,0,0),
			[](int&,const stringUTF8 &strutf8,TextEditLineI *tel,bool isenter)
			{
				if (TextEditLineColorChange(tel,isenter))
				{
					PFE_Cfg["AcceptedPictureFormat"]=OperateForAll<string>(DivideStringByChar(strutf8.cppString(),' '),[](string &s){s=Atoa(s);});
					AcceptedPictureFormat.clear();
					for (const auto &vp:PFE_Cfg["AcceptedPictureFormat"])
						if (vp!="")
							AcceptedPictureFormat.insert(vp);
					PicFileManager.SetPicFileAftername(AcceptedPictureFormat);
				}
			},0);
		tel->SetText(SpliceStringByChar(PFE_Cfg["AcceptedPictureFormat"],' '),0);
		tel->SetBackgroundColor(0,DUC_ClearBackground0);
	}
	
	if (SettingLevel>=SettingLevel_Advanced)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		TinyText *tt=new TinyText(0,fa,{100,0,300,30},PUIT("搜索文本内容的文件后缀名: "),-1);
		TextEditLineI *tel=new TextEditLineI(0,fa,new PosizeEX_Fa6(2,2,300,160,0,0),
			[](int&,const stringUTF8 &strutf8,TextEditLineI *tel,bool isenter)
			{
				if (TextEditLineColorChange(tel,isenter))
				{
					PFE_Cfg["SearchFileContentTarget"]=OperateForAll<string>(DivideStringByChar(strutf8.cppString(),' '),[](string &s){s=Atoa(s);});
					SearchFileContentTarget.clear();
					for (const auto &vp:PFE_Cfg["SearchFileContentTarget"])
						if (vp!="")
							SearchFileContentTarget.insert(vp);
				}
			},0);
		tel->SetText(SpliceStringByChar(PFE_Cfg["SearchFileContentTarget"],' '),0);
		tel->SetBackgroundColor(0,DUC_ClearBackground0);
	}

	if (SettingLevel>=SettingLevel_Easy)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		TinyText *tt=new TinyText(0,fa,{100,0,300,30},PUIT("设置背景图片为(留空为不设置): "),-1);
		TextEditLineI *tel=new TextEditLineI(0,fa,new PosizeEX_Fa6(2,2,300,220,0,0),
			[](int&,const stringUTF8 &strUtf8,TextEditLine <int> *tel,bool isenter)
			{
				bool flag=0;
				if (isenter)
					SetBackgroundPic(strUtf8.cppString(),2);
				TextEditLineColorChange(tel,HaveBackgroundPic);
			},0);
		tel->SetText(PFE_Cfg("BackgroundPicture"),0);
		tel->SetBackgroundColor(0,DUC_ClearBackground0);
		Button <TextEditLineI*> *bu=new Button <TextEditLineI*> (0,fa,new PosizeEX_Fa6(1,2,60,160,0,0),PUIT("浏览"),	
			[](TextEditLineI *&tel)
			{
				new SimpleFileSelectBox(0,[](const string &str)
				{
					PFE_Cfg("LastBgPicSelectPath")=GetPreviousBeforeBackSlash(str);
					return SetBackgroundPic(str,2)?0:1;
				},PFE_Cfg("LastBgPicSelectPath"),AcceptedPictureFormat,600,400,PUIT("请选择背景图片:"));
			},tel);
	}
	
	if (SettingLevel>=SettingLevel_Easy)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		TinyText *tt=new TinyText(0,fa,{100,0,300,30},PUIT("界面透明度(存在背景图片时)"),-1);
		FullFillSlider <int> *ffs=new FullFillSlider <int> (0,fa,new PosizeEX_Fa6(2,2,400,160,3,3),0,[](int &funcdata,double per,bool isLoose){SetUIWidgetsOpacity(EnsureInRange(per*100,1,100));},0);
		ffs->SetPercent(WidgetsOpacityPercent/100.0,0);
	}
	
	if (SettingLevel>=SettingLevel_Easy)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		TinyText *tt=new TinyText(0,fa,{100,0,300,30},PUIT("额外覆盖层灰度透明度"),-1);
		FullFillSliderI *ffs1=new FullFillSliderI(0,fa,new PosizeEX_Fa6(2,3,400,210,2,13),0,
			[](int&,double per,bool)
			{
				int a=BackgroundCoverColor.a,rgb=EnsureInRange(per*255,0,255);
				BackgroundCoverColor=RGBA(rgb,rgb,rgb,a);
				SetUIWidgetsOpacity(WidgetsOpacityPercent);
			},0);
		ffs1->SetPercent(BackgroundCoverColor.r/255.0,0);
//		ffs1->SetBarColor(
		FullFillSliderI *ffs2=new FullFillSliderI(0,fa,new PosizeEX_Fa6(2,1,400,210,13,2),0,
			[](int&,double per,bool)
			{
				BackgroundCoverColor=BackgroundCoverColor.AnotherA(EnsureInRange(per*255,1,255));
				SetUIWidgetsOpacity(WidgetsOpacityPercent);
			},0);
		ffs2->SetPercent(BackgroundCoverColor.a/255.0,0);
		SwitchButtonI *sb=new SwitchButtonI(0,fa,new PosizeEX_Fa6(1,2,40,160,7,7),EnableBackgroundCoverColor,
			[](int&,bool onoff)
			{
				EnableBackgroundCoverColor=onoff;
				PFE_Cfg("EnableBackgroundCoverColor")=onoff?"1":"0";
				SetUIWidgetsOpacity(WidgetsOpacityPercent);
			},0);
	}
	
	if (SettingLevel>=SettingLevel_Easy)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,40));
		fa->SetLayerColor(DUC_SettingItemBackground);
		TinyText *tt=new TinyText(0,fa,{100,0,300,30},PUIT("主题色选择: "),-1);
		using FuncDataType=Doublet<vector <Doublet<int,BorderRectLayer*> >, int>;
		FuncDataType funcdata;
		auto setthemecolor=[](FuncDataType &funcdata)
		{
			if (funcdata.b==CurrentThemeColorCode) return;
			CurrentThemeColorCode=funcdata.b;
			PFE_Cfg("CurrentThemeColorCode")=llTOstr(CurrentThemeColorCode);
			ThemeColor=PUI_ThemeColor(funcdata.b);
			SetUIWidgetsOpacity(WidgetsOpacityPercent);
			for (int i=0;i<funcdata.a.size();++i)
				if (funcdata.a[i].a==funcdata.b)
					funcdata.a[i].b->SetBorderColor(ThemeColor[5]);
				else funcdata.a[i].b->SetBorderColor(RGBA_TRANSPARENT);
		};
		int cnt=PUI_ThemeColor::PUI_ThemeColor_UserDefined;
		for (int i=0;i<cnt;++i)
		{
			auto *lay=new BorderRectLayer(0,fa,new PosizeEX_Fa6(1,2,32,160+i*40-1,2,2));
			lay->SetBorderWidth(2);
			if (i!=CurrentThemeColorCode)
				lay->SetBorderColor(RGBA_TRANSPARENT);
			else lay->SetBorderColor(ThemeColor[5]);
			funcdata.a.push_back({i,lay});
		}
		for (int i=0;i<cnt;++i)
		{
			funcdata.b=i;
			auto *bu=new Button <FuncDataType> (0,fa,new PosizeEX_Fa6(1,2,24,160+i*40+3,8,8),"",setthemecolor,funcdata);
			PUI_ThemeColor tmp(i);
			for (int j=0;j<=2;++j)
				bu->SetButtonColor(j,tmp[j*2+1]);
		}
	}

	
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval*2,0,80));
		fa->SetLayerColor(DUC_SettingItemBackground);
		TinyText *tt=new TinyText(0,fa,{50,30,300,40},PUIT("关于"),-1);
		tt->SetFontSize(18);
		tt->SetTextColor(ThemeColorM[7]);
	}
	
	if (SettingLevel>=SettingLevel_Easy)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		new TinyText(0,fa,new PosizeEX_Fa6(2,2,100,0,0,0),PUIT("软件名:"),-1);
		new TinyText(0,fa,new PosizeEX_Fa6(2,2,300,0,0,0),PUIT("PAL_FileExplorer"),-1);
	}
	
	if (SettingLevel>=SettingLevel_Easy)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		new TinyText(0,fa,new PosizeEX_Fa6(2,2,100,0,0,0),PUIT("当前版本:"),-1);
		new TinyText(0,fa,new PosizeEX_Fa6(2,2,300,0,0,0),ProgramVersion,-1);
	}
	
	if (SettingLevel>=SettingLevel_Easy)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		new TinyText(0,fa,new PosizeEX_Fa6(2,2,100,0,0,0),PUIT("开发者:"),-1);
		new TinyText(0,fa,new PosizeEX_Fa6(2,2,300,0,0,0),PUIT("qianpinyi"),-1);
	}
	
	if (SettingLevel>=SettingLevel_Normal)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		new TinyText(0,fa,new PosizeEX_Fa6(2,2,100,0,0,0),PUIT("创建日期:"),-1);
		new TinyText(0,fa,new PosizeEX_Fa6(2,2,300,0,0,0),PUIT("2021.6.25"),-1);
	}
	
	if (SettingLevel>=SettingLevel_Normal)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		new TinyText(0,fa,new PosizeEX_Fa6(2,2,100,0,0,0),PUIT("当前版本日期:"),-1);
		new TinyText(0,fa,new PosizeEX_Fa6(2,2,300,0,0,0),ProgramVersionDate,-1);
	}
	
	if (SettingLevel>=SettingLevel_Advanced)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		new TinyText(0,fa,new PosizeEX_Fa6(2,2,100,0,0,0),PUIT("当前PAL_Library版本:"),-1);
		new TinyText(0,fa,new PosizeEX_Fa6(2,2,300,0,0,0),llTOstr(PAL_Library_Version_Main)+"."+llTOstr(PAL_Library_Version_Sub)+"."+llTOstr(PAL_Library_Version_Third),-1);
	}
	
	if (SettingLevel>=SettingLevel_Normal)
	{
		Layer *fa=new Layer(0,LarLay_Setting->LargeArea(),new PosizeEX_InLarge(LarLay_Setting,EachSettingInterval,0,30));
		fa->SetLayerColor(DUC_SettingItemBackground);
		new TinyText(0,fa,new PosizeEX_Fa6(2,2,100,0,0,0),PUIT("开发者邮箱:"),-1);
		auto Bu_mail=new Button <Widgets*> (0,fa,new PosizeEX_Fa6(3,2,300,200,0,0),PUIT("qianpinyi@outlook.com"),
			[](Widgets *&funcdata)->void
			{
				ShellExecuteW(0,L"open",L"mailto:qianpinyi@outlook.com",L"",L"",SW_SHOWNORMAL);
				((Button<Widgets*>*)funcdata)->SetTextColor({0,100,255,255});
			},NULL);
		Bu_mail->GetFuncData()=Bu_mail;
		Bu_mail->SetButtonColor(0,RGBA_TRANSPARENT);
	}
	
	LarLay_Setting->SetMinLargeAreaSize(EachSettingMinWidth,0);//??? no effect
}
//End of SettingPage

PFE_Path PFE_Path_GetFa(const PFE_Path &path,bool NoSystem)
{
	if (!path||InThisSet(path,PFE_Path_MainPage,PFE_Path_Setting,PFE_Path_RecycleBin))
		return PFE_Path();
	if (path.str.find("\\")==path.str.npos)
		return PFE_Path_MainPage;//??
	string faPath=GetPreviousBeforeBackSlash(path.str);
	if (faPath.find("\\")!=faPath.npos)
		return PFE_Path(PFE_PathType_Dir/*??*/,faPath,GetLastAfterBackSlash(faPath));
	else//??not only drives
	{
		if (NoSystem)
			return PFE_Path(PFE_PathType_Volume,faPath,faPath);
		switch (GetDriveType(faPath.c_str()))
		{
			case DRIVE_UNKNOWN:
			case DRIVE_REMOTE:
				//...??
			default:
				LogicalDriveInfo info=GetLogicalDriveInfo(faPath);
				return PFE_Path(PFE_PathType_Volume,faPath,DeleteEndBlank(info.name)+"("+faPath+")");
		}
	}
}

set <string> GettingInfoRemoteDrive;
long long GetAllFileInDir(const PFE_Path &path,vector <PFE_Path> &vec,int to)
{
//	DD[0]<<"GetAllFileIn "<<(path.str.empty()?path.name:path.str)<<endl;
	switch (path.type)
	{
		case PFE_PathType_Dir:
		case PFE_PathType_Volume:
		{
			wstring wstr=Utf8ToUnicode(path.str+"\\*");
			long long hFiles=0;
			_wfinddatai64_t da;
			if ((hFiles=_wfindfirsti64(wstr.c_str(),&da))!=-1)
				do
				{
					string name=DeleteEndBlank(UnicodeToUtf8(da.name));
					if (name!="."&&name!=".."&&(to==GetAllFileInDir_To_RecycleBin||ShowHiddenFile||!(da.attrib&_A_HIDDEN)))
						vec.push_back(PFE_Path(da.attrib&_A_SUBDIR?PFE_PathType_Dir:PFE_PathType_File,path.str+"\\"+name,name));
				}
				while (_wfindnexti64(hFiles,&da)==0);
			_findclose(hFiles);
			if (InThisSet(to,GetAllFileInDir_To_BVT,GetAllFileInDir_To_LVT))
				if (ShowSensizeResultMode==2&&LoadedSensize.IsSensizeOK()&&LoadedSensize.GetBasePath()==path)
					vec.push_back(PFE_Path(PFE_PathType_VirtualDir,LoadedSensize.GetBasePath().str,PUIT("虚拟的")+path.name));
				else if (ShowSensizeResultMode==3&&LoadedSensize.IsSensizeOK())
				{
					auto vChilds=LoadedSensize.GetChilds(path);//??
					set <string> se;
					for (auto vp:vec)
						se.insert(vp.str);
					for (auto vp:vChilds)
						if (se.find(path.str+"\\"+vp.name)==se.end())
							vec.push_back(PFE_Path(vp.IsDir?PFE_PathType_VirtualDir:PFE_PathType_VirtualFile,path.str+"\\"+vp.name,vp.name));
				}
			return vec.size();
		}
		case PFE_PathType_MainPage:
		{
			vector <string> diskDrive=GetAllLogicalDrive();
			for (auto vp:diskDrive)
				switch (GetDriveType(vp.c_str()))
				{
					case DRIVE_REMOTE:
					{
						PFE_Path *p=new PFE_Path(PFE_PathType_Volume,vp,vp,PFE_Path_VolumeCode_Remote);
						vec.push_back(*p);
						if (to==GetAllFileInDir_To_BVT||to==GetAllFileInDir_To_LVT)
							if (GettingInfoRemoteDrive.find(vp)==GettingInfoRemoteDrive.end())
							{
								GettingInfoRemoteDrive.insert(vp);
								PAL_Thread <int,PFE_Path> ([](PFE_Path &path)->int
								{
									PUI_SendFunctionEvent<Doublet <LogicalDriveInfo,PFE_Path> >([](Doublet <LogicalDriveInfo,PFE_Path> &data)
									{
										auto &[info,path]=data;
										GettingInfoRemoteDrive.erase(path.str);
										int pos=MainView_Find(path);
										if (pos!=-1)
										{
											MainViewData &data=MainView_GetFuncData(pos);
											if (info.Succeed)
											{	
												data.TT_Name->SetText(data.path.name=info.name+"("+path.str+")");
												data.ProBar_Space->SetPercent(1-info.TotalNumberOfFreeBytes*1.0/info.TotalNumberOfBytes);
												data.ProBar_Space->SetFullColor(RGBA_RED);
												if (info.TotalNumberOfFreeBytes<info.TotalNumberOfBytes*0.1)
													data.ProBar_Space->SetBarColor({218,38,38,255});
												data.TT_Second->SetText(GetFileSizeString(info.TotalNumberOfFreeBytes)+PUIT(" 可用, 共 ")+GetFileSizeString(info.TotalNumberOfBytes));
												MainView_UpdateIcon(pos,GetItemIcon(path,0,FileIconType_RemoteDrive,1),15);
											}
											else data.TT_Second->SetText(PUIT("获取远程驱动器信息失败."));
										}
									},{GetLogicalDriveInfo(path.str),path});
									return 0;
								},*p).Detach();
							}
						break;
					}
					default:
					{
						LogicalDriveInfo info=GetLogicalDriveInfo(vp);
						vec.push_back(PFE_Path(PFE_PathType_Volume,vp,DeleteEndBlank(info.name)+"("+vp+")"));
						break;
					}
					case DRIVE_UNKNOWN:
						break;
				}
			return vec.size();
		}
		case PFE_PathType_VirtualDir:
		{
			if (InThisSet(to,GetAllFileInDir_To_BVT,GetAllFileInDir_To_LVT)&&LoadedSensize.IsSensizeOK()&&InThisSet(ShowSensizeResultMode,2,3))
			{
				auto vChilds=LoadedSensize.GetChilds(path);//??
				set <string> se;
				if (ShowSensizeResultMode==3)
					for (auto vp:vec)
						se.insert(vp.str);
				for (auto vp:vChilds)
					if (se.find(path.str+"\\"+vp.name)==se.end())
						vec.push_back(PFE_Path(vp.IsDir?PFE_PathType_VirtualDir:PFE_PathType_VirtualFile,path.str+"\\"+vp.name,vp.name));
			}
			return vec.size();
		}
		case PFE_PathType_Library:
		case PFE_PathType_Network:
		case PFE_PathType_Cloud:
		case PFE_PathType_File:
		default:
			DD[2]<<"Cannot get all file in "<<(path.str.empty()?path.name:path.str)<<endl;
			return -1;
	}
}

void SetLeftListItem(int p,const PFE_Path &path,int targetList)//targetList 0:QuickList 1:RecentList
{
	Widgets *fa=(targetList==0?LVT_QuickList:LVT_RecentList)->SetListContent(p,LVT_QuickListData(path,targetList));
	LVT_QuickListData &data=(targetList==0?LVT_QuickList:LVT_RecentList)->GetFuncData(p);
	data.TT_Name=new TinyText(0,fa,new PosizeEX_Fa6(2,2,40,10,0,0),path.name,-1);
	data.TT_FullPath=new TinyText(0,fa,new PosizeEX_Fa6(2,2,240,10,0,0),path.str,1,ThemeColorBG[5]);
	if (targetList==1)
		if (LRU_RecentList.Insert(path,0)==0)
			DD[2]<<"LRU_RecentList cannot insert!"<<endl;
}

void SetMainViewContent(int p,const PFE_Path &path)
{
	RGBA co012[3];
	RGBA &co0=co012[0],&co1=co012[1],&co2=co012[2];
	for (int i=0;i<=2;++i)
		switch (path.type)
		{
			case PFE_PathType_Dir:			co012[i]=DUC_DirBlockColor[i];		break;
			case PFE_PathType_File:			co012[i]=DUC_FileBlockColor[i];		break;
			case PFE_PathType_Volume:		co012[i]=DUC_VolumeBlockColor[i];	break;
			case PFE_PathType_VirtualDir:	co012[i]=DUC_VDirBlockColor[i];		break;
			case PFE_PathType_VirtualFile:	co012[i]=DUC_VFileBlockColor[i];	break;
			default:						co012[i]=DUC_ClearThemeColor[i*2];	break;
		}
	
	int RecentLevel=-1;
	if (EnableRecentFileHighLight!=0&&InThisSet(path.type,PFE_PathType_Dir,PFE_PathType_File))
	{
		_wfinddatai64_t da;
		long long hFiles=_wfindfirsti64(Utf8ToUnicode(path.str).c_str(),&da);//Slow???
		_findclose(hFiles);
		RecentLevel=GetRecentFileHighLightLevel(PAL_TimeP(*localtime(&da.time_create)),TimeForSetCurrentPath);
	}
	
	auto ChooseItemFunc=[](PFE_Path &path,bool onoff)
	{
		SetSelectMainViewItem(path,onoff);
	};
	
	auto SetFileSizeToTinyText=[](const PFE_Path &path,TinyText *TT)
	{
		_wfinddatai64_t da;
		long long hFiles=_wfindfirsti64(Utf8ToUnicode(path.str).c_str(),&da);//Slow???
		_findclose(hFiles);
		TT->SetText(GetFileSizeString(da.size));
	};
	
	auto SetSensizeResultToTinyText=[](const PFE_Path &path,TinyText *TT)//ShouldCall CurrentSensize.Find() first!
	{
		unsigned long long size=CurrentSensize.GetLastFoundSize(),
						   per1=CurrentSensize.GetLastFoundLocalPercent()*10000,
						   per2=CurrentSensize.GetLastFoundGlobalPercent()*10000;
		if (ShowSensizeResultMode==1)
			TT->SetText(GetFileSizeString(size)+" | "+llTOstr(per1/100)+"."+llTOstr(per1%100)+"% | "+llTOstr(per2/100)+"."+llTOstr(per2%100)+"%");
		else if (ShowSensizeResultMode==3)
			if (LoadedSensize.Find(path))
			{
				unsigned long long lastSize=LoadedSensize.GetLastFoundSize();
				TT->SetText(GetFileSizeString(size)+(lastSize==size?"":string(" (")+(size>=lastSize?"+":"-")+GetFileSizeString(llabs(lastSize-size))+")")+" | "+llTOstr(per1/100)+"."+llTOstr(per1%100)+"% | "+llTOstr(per2/100)+"."+llTOstr(per2%100)+"%");
			}
			else TT->SetText(GetFileSizeString(size)+" (+) | "+llTOstr(per1/100)+"."+llTOstr(per1%100)+"% | "+llTOstr(per2/100)+"."+llTOstr(per2%100)+"%");
		TT->SetTextColor(DUC_SensizeTextColor);
	};
	
	auto SetDriveInfoToBarAnsText=[](const PFE_Path &path,ProgressBar *PB,TinyText *TT)
	{
		if (path.code==PFE_Path_VolumeCode_Normal)
		{
			LogicalDriveInfo info=GetLogicalDriveInfo(path.str);
			PB->SetPercent(1-info.TotalNumberOfFreeBytes*1.0/info.TotalNumberOfBytes);
			PB->SetFullColor(RGBA_RED);
			if (info.TotalNumberOfFreeBytes<info.TotalNumberOfBytes*0.1)
				PB->SetBarColor({218,38,38,255});
			TT->SetText(GetFileSizeString(info.TotalNumberOfFreeBytes)+PUIT(" 可用, 共 "+GetFileSizeString(info.TotalNumberOfBytes)));
		}
		else TT->SetText(PUIT("远程驱动器信息获取中..."));
	};
	
	auto SetVirtualInfoToTinyText=[](const PFE_Path &path,TinyText *TT)
	{
		if (InThisSet(ShowSensizeResultMode,2,3))
			if (LoadedSensize.Find(path))
				if (path.type==PFE_PathType_VirtualDir)
					TT->SetText(PUIT(ShowSensizeResultMode==3?"[虚拟文件夹] 已移除 | -":"[虚拟文件夹] ")+GetFileSizeString(LoadedSensize.GetLastFoundSize()));
				else if (PFE_PathType_VirtualFile)
					TT->SetText(PUIT(ShowSensizeResultMode==3?"[虚拟文件] 已移除 | -":"[虚拟文件] ")+GetFileSizeString(LoadedSensize.GetLastFoundSize()));
	};
	
	auto SetNameTextWithExtraData=[RecentLevel](const PFE_Path &path,TinyText *TT)
	{
		TT->SetText(ShowAfternameInName||NotInSet(path.type,PFE_PathType_File,PFE_PathType_VirtualFile)?path.name:GetWithOutAftername(path.name));
		if (InRange(RecentLevel,0,2)&&(EnableRecentFileHighLight&1))
			TT->SetTextColor(GetRecentFileHighLightColor(RecentLevel));
	};
	
	auto SetPicDelayLoadFunc=[&path](PictureBox <Triplet <PFE_Path,int,int> > *pb,int picSize=80)
	{
		pb->SetFuncData({path,picSize,0});
		pb->SetDelayLoadFunc([](Triplet <PFE_Path,int,int> &data,PictureBox <Triplet <PFE_Path,int,int> > *pb)->int
		{
			SharedTexturePtr tex=GetItemIcon(data.a,data.b,FileIconType_Auto,CurrentMainView==CurrentMainView_BVT_RecycleBin?3:(CurrentMainView==CurrentMainView_LVT_SearchResult?2:0));
			data.c=5;
			if (!!tex)
				pb->SetPicture(tex,11);
			return 1;
		});
	};

	switch (CurrentMainView)
	{
		case CurrentMainView_BVT_0:
		case CurrentMainView_BVT_RecycleBin:
		{
			Widgets *fa=BVT_MainView->SetBlockContent(p,MainViewData(path),co0,co1,co2);
			MainViewData &data=BVT_MainView->GetFuncData(p);
			data.fa=fa;
			data.PB_Pic=new PictureBox <Triplet <PFE_Path,int,int> > (0,fa,new PosizeEX_Fa6(3,2,0,80,0,0));
			SetPicDelayLoadFunc(data.PB_Pic,80);
			data.CB_Choose=new BorderCheckBox <PFE_Path> (0,data.PB_Pic,new PosizeEX_Fa6_Full,0,ChooseItemFunc,path);
			data.TT_Name=new TinyText(0,fa,new PosizeEX_Fa6(2,3,90,5,0,30),"",-1);
			SetNameTextWithExtraData(path,data.TT_Name);
			switch (path.type)
			{
				case PFE_PathType_File:
				{
					data.TT_First=new TinyText(0,fa,new PosizeEX_Fa6(2,3,90,5,30,25),GetAftername(path.name)+PUIT("文件"),-1);
					data.TT_Second=new TinyText(0,fa,new PosizeEX_Fa6(2,3,90,5,55,25),"",-1);
					if (CurrentMainView==CurrentMainView_BVT_RecycleBin||ShowSensizeResultMode==0||!CurrentSensize.IsSensizeOK())
						SetFileSizeToTinyText(path,data.TT_Second);
					else if (CurrentSensize.Find(path))
						SetSensizeResultToTinyText(path,data.TT_Second);
					break;
				}
				case PFE_PathType_Volume:
				{
					data.ProBar_Space=new ProgressBar(0,fa,new PosizeEX_Fa6(2,3,90,5,32,20));
					data.TT_Second=new TinyText(0,fa,new PosizeEX_Fa6(2,3,90,5,55,25),"",-1);
					SetDriveInfoToBarAnsText(path,data.ProBar_Space,data.TT_Second);
					break;
				}
				case PFE_PathType_Dir:
				{
					data.TT_Second=new TinyText(0,fa,new PosizeEX_Fa6(2,3,90,5,55,25),"",-1);
					if (CurrentMainView!=CurrentMainView_BVT_RecycleBin&&ShowSensizeResultMode!=0&&CurrentSensize.IsSensizeOK())
						if (CurrentSensize.Find(path))
							SetSensizeResultToTinyText(path,data.TT_Second);
					break;
				}
				case PFE_PathType_VirtualDir:
				{
					data.TT_Second=new TinyText(0,fa,new PosizeEX_Fa6(2,3,90,5,55,25),PUIT("[虚拟文件夹]"),-1,DUC_SensizeTextColor);
					SetVirtualInfoToTinyText(path,data.TT_Second);
					break;
				}
				case PFE_PathType_VirtualFile:
				{
					data.TT_First=new TinyText(0,fa,new PosizeEX_Fa6(2,3,90,5,30,25),GetAftername(path.name)+PUIT("文件"),-1);
					data.TT_Second=new TinyText(0,fa,new PosizeEX_Fa6(2,3,90,5,55,25),PUIT("[虚拟文件]"),-1,DUC_SensizeTextColor);
					SetVirtualInfoToTinyText(path,data.TT_Second);
					break;
				}	
			}
			break;
		}
		case CurrentMainView_BVT_1:
		case CurrentMainView_BVT_2:
		case CurrentMainView_BVT_3:
		case CurrentMainView_BVT_4:
		case CurrentMainView_BVT_5:
		case CurrentMainView_BVT_6:
		case CurrentMainView_BVT_7:
		case CurrentMainView_BVT_8:
		case CurrentMainView_BVT_9:
		case CurrentMainView_BVT_10:
		{
			Widgets *fa=BVT_MainView->SetBlockContent(p,MainViewData(path),co0,co1,co2);
			MainViewData &data=BVT_MainView->GetFuncData(p);
			data.fa=fa;
			data.PB_Pic=new PictureBox <Triplet <PFE_Path,int,int> > (0,fa,new PosizeEX_Fa6(2,2,0,0,0,30));
			SetPicDelayLoadFunc(data.PB_Pic,160+(CurrentMainView-5)*20);
			data.CB_Choose=new BorderCheckBox <PFE_Path> (0,data.PB_Pic,new PosizeEX_Fa6_Full,0,ChooseItemFunc,path);
			data.TT_Name=new TinyText(0,fa,new PosizeEX_Fa6(2,1,0,0,30,0),"");
			SetNameTextWithExtraData(path,data.TT_Name);
			break;
		}
		case CurrentMainView_LVT_Normal:
		case CurrentMainView_LVT_SearchResult:
		{
			Widgets *fa=LVT_MainView->SetListContent(p,MainViewData(path),{co0,co1,co2});
			MainViewData &data=LVT_MainView->GetFuncData(p);
			data.fa=fa;
			data.PB_Pic=new PictureBox <Triplet <PFE_Path,int,int> > (0,fa,new PosizeEX_Fa6(3,2,50,40,0,0));
			data.TT_Name=new TinyText(0,fa,new PosizeEX_Fa6(2,2,100,-0.3,0,0),"",-1);
			SetNameTextWithExtraData(path,data.TT_Name);
			SetPicDelayLoadFunc(data.PB_Pic,40);
			data.CB_Choose=new CheckBox <PFE_Path> (0,fa,new PosizeEX_Fa6(3,2,10,30,5,5),0,ChooseItemFunc,path);
			if (CurrentMainView==CurrentMainView_LVT_SearchResult)
				data.TT_Second=new TinyText(0,fa,new PosizeEX_Fa6(2,2,400,20,0,0),path.str,1,ThemeColorBG[4]);
			else switch (path.type)
			{
				case PFE_PathType_File:
				{
					data.TT_Second=new TinyText(0,fa,new PosizeEX_Fa6(2,2,400,20,0,0),path.str,1,ThemeColorBG[4]);
					if (ShowSensizeResultMode==0||!CurrentSensize.IsSensizeOK())
						SetFileSizeToTinyText(path,data.TT_Second);
					else if (CurrentSensize.Find(path))
						SetSensizeResultToTinyText(path,data.TT_Second);
					break;
				}
				case PFE_PathType_Volume:
				{
					data.ProBar_Space=new ProgressBar(0,fa,new PosizeEX_Fa6(2,2,400,50,5,5));
					data.TT_Second=new TinyText(0,data.ProBar_Space,new PosizeEX_Fa6(2,2,0,0,0,0),"",0);
					SetDriveInfoToBarAnsText(path,data.ProBar_Space,data.TT_Second);
					break;
				}
				case PFE_PathType_Dir:
				{
					data.TT_Second=new TinyText(0,fa,new PosizeEX_Fa6(2,2,400,20,0,0),path.str,1,ThemeColorBG[4]);
					if (ShowSensizeResultMode!=0&&CurrentSensize.IsSensizeOK())
						if (CurrentSensize.Find(path))
							SetSensizeResultToTinyText(path,data.TT_Second);
					break;
				}
				case PFE_PathType_VirtualDir:
				case PFE_PathType_VirtualFile:
				{
					data.TT_Second=new TinyText(0,fa,new PosizeEX_Fa6(2,2,400,20,0,0),PUIT(path.type==PFE_PathType_VirtualDir?"[虚拟文件夹]":"[虚拟文件]"),1,DUC_SensizeTextColor);
					SetVirtualInfoToTinyText(path,data.TT_Second);
					break;
				}
				default:
					data.TT_Second=new TinyText(0,fa,new PosizeEX_Fa6(2,2,400,20,0,0),path.str,1,ThemeColorBG[4]);
					break;
			}
			break;
		}	
		default:
			DD[2]<<"Wrong CurrentMainView,please check the source code! "<<endl;
			break;
	}
	
	if (InRange(RecentLevel,0,2)&&(EnableRecentFileHighLight&2))
	{
		MainViewData &mvd=MainView_GetFuncData(p);
		mvd.BRL_Border=new BorderRectLayer(0,mvd.fa,new PosizeEX_Fa6_Full);
		mvd.BRL_Border->SetBorderColor(GetRecentFileHighLightColor(RecentLevel));
	}
}

void PushbackMainViewContent(const PFE_Path &path)
{SetMainViewContent(1e9/*?*/,path);}

enum
{
	PathSortFunc_Normal=0,
	PathSortFunc_None,
	PathSortFunc_LexicographicalOrder,
	PathSortFunc_FileSize,
	PathSortFunc_FileSizeDelta,
	PathSortFunc_Aftername,
	PathSortFunc_WriteTime,
	PathSortFunc_CreateTime,
};

int TypeMapForSortWithVirtualDirFile(int t)
{
	switch (t)
	{
		case PFE_PathType_Dir:			return 1;
		case PFE_PathType_VirtualDir:	return 2;
		case PFE_PathType_File:			return 3;
		case PFE_PathType_VirtualFile:	return 4;
		default:						return 0;
	}
};

void SortNodes(const PFE_Path &curpath,vector <PFE_Path> &nodes)
{
	auto getfilesize=[](const PFE_Path &path)->unsigned long long//efficiency??
	{
		switch (path.type)
		{
			case PFE_PathType_File:
			{
				_wfinddatai64_t da;
				long long hFiles=_wfindfirsti64(Utf8ToUnicode(path.str).c_str(),&da);//Slow???
				_findclose(hFiles);
				return da.size;				
			}
			case PFE_PathType_Volume:
				if (path.code==PFE_Path_VolumeCode_Normal)
				{
					LogicalDriveInfo info=GetLogicalDriveInfo(path.str);
					return info.TotalNumberOfBytes-info.TotalNumberOfFreeBytes;
				}
				else return 0;
			default:	return 0;
		}
	};
	switch (PathSortMode)
	{                          
		LABEL_PathSortFunc_Normal:
		default:
		case PathSortFunc_Normal:
			sort(nodes.begin(),nodes.end(),[](const PFE_Path &a,const PFE_Path &b)->bool
			{
				if (a.type==b.type)
					if (a.type==PFE_PathType_Volume)
						if (a.code!=b.code) return a.code<b.code;
						else if (PathSortReversed) return SortComp_WithNum(b.str,a.str);
						else return SortComp_WithNum(a.str,b.str);
					else if (PathSortReversed) return SortComp_WithNum(b.name,a.name);
					else return SortComp_WithNum(a.name,b.name);
				else return TypeMapForSortWithVirtualDirFile(a.type)<TypeMapForSortWithVirtualDirFile(b.type);
			});
			break;
		case PathSortFunc_None:
			break;
		case PathSortFunc_LexicographicalOrder:
			sort(nodes.begin(),nodes.end(),[](const PFE_Path &a,const PFE_Path &b)->bool
			{
				if (PathSortReversed) return b.name<a.name;
				else return a.name<b.name;
			});
			break;
		case PathSortFunc_FileSize:
		{
			vector <Doublet<unsigned long long,PFE_Path> > vec;
			if (InThisSet(ShowSensizeResultMode,1,3)&&CurrentSensize.Find(curpath))//??
				for (auto vp:nodes)
					vec.push_back({InThisSet(vp.type,PFE_PathType_VirtualDir,PFE_PathType_VirtualFile)?(LoadedSensize.Find(vp)?LoadedSensize.GetLastFoundSize():0):(CurrentSensize.Find(vp)?CurrentSensize.GetLastFoundSize():getfilesize(vp)),vp});
			else if (ShowSensizeResultMode==2&&LoadedSensize.Find(curpath))
				for (auto vp:nodes)
					if (InThisSet(vp.type,PFE_PathType_VirtualDir,PFE_PathType_VirtualFile))
						vec.push_back({LoadedSensize.Find(vp)?LoadedSensize.GetLastFoundSize():0,vp});
					else vec.push_back({getfilesize(vp),vp});
			else
				for (auto vp:nodes)
					vec.push_back({getfilesize(vp),vp});
			nodes.clear();
			sort(vec.begin(),vec.end(),[](const Doublet<unsigned long long,PFE_Path> &x,const Doublet<unsigned long long,PFE_Path> &y)->bool
			{
				if (x.b.type==y.b.type)
					if (PathSortReversed) return x.a<y.a;
					else return x.a>y.a;
				else return TypeMapForSortWithVirtualDirFile(x.b.type)<TypeMapForSortWithVirtualDirFile(y.b.type);
			});
			for (auto vp:vec)
				nodes.push_back(vp.b);
			break;
		}
		case PathSortFunc_FileSizeDelta:
		{
			vector <Doublet<long long,PFE_Path> > vec;
			if (ShowSensizeResultMode==3&&CurrentSensize.Find(curpath))//??
				for (auto vp:nodes)
				{
					bool c=CurrentSensize.Find(vp),
						 l=LoadedSensize.Find(vp);
					if (c&&l)
						vec.push_back({(long long)CurrentSensize.GetLastFoundSize()-(long long)LoadedSensize.GetLastFoundSize(),vp});
					else if (c)
						vec.push_back({(long long)CurrentSensize.GetLastFoundSize(),vp});
					else if (l)
						vec.push_back({-(long long)LoadedSensize.GetLastFoundSize(),vp});
					else vec.push_back({0,vp});
				}
			else goto LABEL_PathSortFunc_Normal;
			nodes.clear();
			sort(vec.begin(),vec.end(),[](const Doublet<long long,PFE_Path> &x,const Doublet<long long,PFE_Path> &y)->bool
			{
				if (x.b.type==y.b.type)
					if (PathSortReversed) return x.a<y.a;
					else return x.a>y.a;
				else return TypeMapForSortWithVirtualDirFile(x.b.type)<TypeMapForSortWithVirtualDirFile(y.b.type);
			});
			for (auto vp:vec)
				nodes.push_back(vp.b);
			break;
		}
		case PathSortFunc_Aftername:
			sort(nodes.begin(),nodes.end(),[](const PFE_Path &a,const PFE_Path &b)->bool
			{
				if (a.type==b.type)
					if (InThisSet(a.type,PFE_PathType_File,PFE_PathType_VirtualFile))
						if (PathSortReversed) return Atoa(GetAftername(GetLastAfterBackSlash(b.str)))<Atoa(GetAftername(GetLastAfterBackSlash(a.str)));
						else return Atoa(GetAftername(GetLastAfterBackSlash(a.str)))<Atoa(GetAftername(GetLastAfterBackSlash(b.str)));
					else 
						if (PathSortReversed) return SortComp_WithNum(b.name,a.name);
						else return SortComp_WithNum(a.name,b.name);
				else return TypeMapForSortWithVirtualDirFile(a.type)<TypeMapForSortWithVirtualDirFile(b.type);
			});
			break;
		case PathSortFunc_WriteTime:
		case PathSortFunc_CreateTime:
		{
			vector <Doublet<PAL_TimeP,PFE_Path> > vec;
			for (auto vp:nodes)
				if (InThisSet(vp.type,PFE_PathType_Dir,PFE_PathType_File))//??
				{
					_wfinddatai64_t da;
					long long hFiles=_wfindfirsti64(Utf8ToUnicode(vp.str).c_str(),&da);//Slow???
					_findclose(hFiles);
					tm *p=localtime(PathSortMode==PathSortFunc_WriteTime?&da.time_write:&da.time_create);
					vec.push_back({p==NULL?PAL_TimeP():PAL_TimeP(*p),vp});
				}
				else if (InThisSet(vp.type,PFE_PathType_VirtualDir,PFE_PathType_VirtualFile)&&LoadedSensize.Find(vp))
					vec.push_back({PathSortMode==PathSortFunc_WriteTime?LoadedSensize.GetLastFoundNode().WriteTime:LoadedSensize.GetLastFoundNode().CreateTime,vp});
				else vec.push_back({PAL_TimeP(),vp});
			nodes.clear();
			sort(vec.begin(),vec.end(),[](const Doublet<PAL_TimeP,PFE_Path> &x,const Doublet<PAL_TimeP,PFE_Path> &y)->bool
			{
				if (x.b.type==y.b.type)
					if (PathSortReversed) return x.a>y.a;
					else return x.a<y.a;
				else return TypeMapForSortWithVirtualDirFile(x.b.type)<TypeMapForSortWithVirtualDirFile(y.b.type);
			});
			for (auto vp:vec)
				nodes.push_back(vp.b);
			break;
		}
	}
}

void SetCurrentSortMode(int mode,bool reverse,bool refresh);

void SetCurrentPath(const PFE_Path &tar,int from)
{
	if (!tar||NotInSet(from,SetCurrentPathFrom_Refresh,SetCurrentPathFrom_TabView)&&tar==CurrentPath) return;
	
	PFE_Path LastPath=CurrentPath;
	
	if (tar!=CurrentPath)
		SetDirectoryMonitorTarget(tar);
	
	if (from==SetCurrentPathFrom_GoBack)
		CurrentPathContext->GoBack();
	else if (from==SetCurrentPathFrom_GoForward)
		CurrentPathContext->GoForward();
	else if (from!=SetCurrentPathFrom_Refresh&&from!=SetCurrentPathFrom_TabView)
		CurrentPathContext->PushNew();
	CurrentPath=tar;
	
	ThreadGetItemIcon.Clear();
	
	TimeForSetCurrentPath=PAL_TimeP::CurrentDateTime();
	
	if (ShowFullPathInWindowTitle==0)
		DoNothing;
	else if (ShowFullPathInWindowTitle==1||CurrentPath.str=="")
		MainWindow->SetWindowTitle(ProgramNameVersion+" | "+CurrentPath.name);
	else MainWindow->SetWindowTitle(ProgramNameVersion+" | "+CurrentPath.str);

	if (CurrentTabMode!=TabMode_None)
		if (TM_Tab->CurrentTabDataLayer()!=nullptr)
			TM_Tab->CurrentTabDataLayer()->SetTitle(CurrentPath.name);

	if (tar.type==PFE_PathType_Setting)
		SetCurrentMainView(CurrentMainView_Setting,1);
	else if (tar.type==PFE_PathType_RecycleBin)
	{
		if (CurrentMainView!=CurrentMainView_BVT_RecycleBin)
			SetCurrentMainView(CurrentMainView_BVT_RecycleBin,1);
	}
	else if (tar.type==PFE_PathType_Search)//Temporarily use,just for test...
	{
		if (CurrentMainView!=CurrentMainView_LVT_SearchResult)
			SetCurrentMainView(CurrentMainView_LVT_SearchResult,1);
	}
	else if (!InRange(CurrentMainView,CurrentMainView_BVT_0,CurrentMainView_LVT_Normal))
		SetCurrentMainView(CurrentMainView_BVT_0,1);
	
	if (RecentListLRULimit!=0&&CurrentPath!=PFE_Path_MainPage)//??
	{
		if (LRU_RecentList.Get(CurrentPath)==NULL)
			SetLeftListItem(0,CurrentPath,1);
		if (PFE_Cfg("EnableRecentListGradualColor")=="1")//??
		{
			auto vec=LRU_RecentList.GetLinkedData();
			for (int i=0;i<vec.size();++i)
			{
				int p=LVT_RecentList->Find(LVT_QuickListData(vec[i].a,1));
				if (p!=-1)
				{
					LVT_QuickListData &data=LVT_RecentList->GetFuncData(p);
					data.TT_Name->SetTextColor(RGBA_WHITE<<RGBA(83,97,225,255-i*20));
					data.NumCode=i+1;
				};
			}
		}
	}
	
	DelayDeleteToNULL(TT_InfoOfDir);
	TT_MainInfo->SetText("");

	vector <PFE_Path> vec;
	PFE_Path p=tar;

	do vec.push_back(p),p=PFE_Path_GetFa(p);
	while (!!p);

	AddrSec_TopAddress->Clear();
	for (int i=vec.size()-1;i>=0;--i)
		AddrSec_TopAddress->PushbackSection(vec[i].name,vec[i]);
	
	if (Th_SearchFile!=NULL)
		StopSearchThread();
	
	if (InRange(CurrentMainView,CurrentMainView_BVT_0,CurrentMainView_LVT_Normal)||InThisSet(CurrentMainView,CurrentMainView_LVT_SearchResult,CurrentMainView_BVT_RecycleBin))
		MainView_ClearContent(from==SetCurrentPathFrom_Refresh),
		MainView_SetBackgroundData(CurrentPath);
	else if (CurrentMainView==CurrentMainView_Setting)
		return;
	else ;//...
	
	if (CurrentMainView==CurrentMainView_LVT_SearchResult)
	{
		int r=(int)LastSearchResult.size()-1;
		for (int i=1;i<=r;++i)
			PushbackMainViewContent(LastSearchResult[i]);
	}
	else if (CurrentMainView==CurrentMainView_BVT_RecycleBin)
	{
		vector <PFE_Path> vec;
		GetAllFileInRecycleBin(vec,CurrentPath);
		for (const auto &vp:vec)
			PushbackMainViewContent(vp);
		if (vec.empty())
			TT_InfoOfDir=new TinyText(0,TwinLay_DivideTreeBlock->AreaB(),new PosizeEX_Fa6(2,3,20,20,50,20),PUIT("回收站为空"));
		TT_MainInfo->SetText(llTOstr(MainView_GetItemCnt())+PUIT(" 个项目"));
	}
	else
	{
		vector <PFE_Path> childNodes;
		GetAllFileInDir(CurrentPath,childNodes,CurrentMainView==CurrentMainView_LVT_Normal?GetAllFileInDir_To_LVT:GetAllFileInDir_To_BVT);
		
		{
			int cntPic=0;
			for (auto vp:childNodes)
				if (vp.type==PFE_PathType_File&&AcceptedPictureFormat.find(Atoa(GetAftername(vp.str)))!=AcceptedPictureFormat.end())
					++cntPic;
			if ((childNodes.size()==0||cntPic<childNodes.size()*(SwitchToPictureViewPercent/100.0))&&InRange(CurrentMainView,CurrentMainView_BVT_1,CurrentMainView_BVT_10))
				SetCurrentMainView(CurrentMainView_BVT_0,1);
			else if (cntPic>=childNodes.size()*(SwitchToPictureViewPercent/100.0)&&!InRange(CurrentMainView,CurrentMainView_BVT_1,CurrentMainView_BVT_10))
				SetCurrentMainView(CurrentMainView_BVT_5,1);
			
			if (cntPic!=0)
				if (InThisSet(CurrentPath.type,PFE_PathType_Dir,PFE_PathType_Volume))
					PicFileManager.SetBasePath(CurrentPath.str);
		}
		
		if (from!=SetCurrentPathFrom_Refresh)//Get current state default sort method
			if (InThisSet(from,SetCurrentPathFrom_GoBack,SetCurrentPathFrom_GoForward)&&CurrentPathContext->CurrentPathWithExtraData.MainViewType==CurrentMainView)
				SetCurrentSortMode(CurrentPathContext->CurrentPathWithExtraData.SortMode,CurrentPathContext->CurrentPathWithExtraData.SortReversed,0);
			else if (CurrentMainView==CurrentMainView_BVT_0&&InThisSet(ShowSensizeResultMode,1,3)&&CurrentSensize.Find(CurrentPath))
				SetCurrentSortMode(PathSortFunc_FileSize,0,0);
			else SetCurrentSortMode(PathSortFunc_Normal,0,0);
		SortNodes(CurrentPath,childNodes);
		
		for (auto vp:childNodes)
			PushbackMainViewContent(vp);
		
		if (EnableAutoSelectLastPath)//??
			if (LastPath!=CurrentPath)
				SetSelectMainViewItem(LastPath,1);
		
		if (InThisSet(from,SetCurrentPathFrom_GoBack,SetCurrentPathFrom_GoForward,SetCurrentPathFrom_TabView)&&CurrentPathContext->CurrentPathWithExtraData.MainViewType==CurrentMainView)
		{
			PUI_UpdateWidgetsPosize(MainWindow);//????
			if (InThisSet(CurrentMainView,CurrentMainView_BVT_0,CurrentMainView_LVT_SearchResult))
				MainView_SetScrollBarPercent(CurrentPathContext->CurrentPathWithExtraData.MainViewScrollPercentY);
		}
		
		if (childNodes.empty())
			TT_InfoOfDir=new TinyText(0,TwinLay_DivideTreeBlock->AreaB(),new PosizeEX_Fa6(2,3,20,20,50,20),PUIT("此文件夹为空"));
		TT_MainInfo->SetText(llTOstr(MainView_GetItemCnt())+PUIT(" 个项目"));
	}
	
	if (from==SetCurrentPathFrom_Refresh)
		RestoreRefreshSelect();
	
	SetNeedUpdateOperationButton();//??
}

void SetCurrentSortMode(int mode,bool reverse,bool refresh)
{
	if (DDB_SortMethod->GetCurrentChoosePos()!=mode)
		DDB_SortMethod->SetSelectChoice(mode,0);
	Bu_ReversedSort->SetButtonText(reverse?PUIT("↑"):PUIT("↓"));
	PathSortMode=mode;
	PathSortReversed=reverse;
	if (refresh)
		SetCurrentPath(CurrentPath,SetCurrentPathFrom_Refresh);
}

void PopupFunc_SearchInThisDir()
{
	auto mbl=new MessageBoxLayer(0,PUIT("在当前目录查找："),400,80);
	mbl->SetClickOutsideReaction(1);
	mbl->EnableShowTopAreaColor(1);
	mbl->SetBackgroundColor(DUC_MessageLayerBackground);
	mbl->SetTopAreaColor(DUC_MessageLayerTopAreaColor);
	auto tel=new TextEditLine <MessageBoxLayer*> (0,mbl,new PosizeEX_Fa6(2,2,20,85,40,10),
		[](MessageBoxLayer *&mbl,const stringUTF8 &strutf8,TextEditLine <MessageBoxLayer*> *tel,bool isenter)
		{
			if (isenter)
			{
				regex pattern;
				try 
				{
					pattern=regex(strutf8.cppString(),regex::optimize|regex::icase|regex::nosubs|regex::ECMAScript);//??
				}
				catch(regex_error)
				{
					WarningMessageButton(SetSystemBeep_Error,PUIT("错误"),PUIT("正则表达式有误！"));
					mbl->Close();
					return;
				}
				for (int i=0,p=MainView_GetCurrentSelectItem()+1;i<=MainView_GetItemCnt();++i)
					if (i==MainView_GetItemCnt()) SetSystemBeep(SetSystemBeep_Warning);
					else
					{
						MainViewData &mvd=MainView_GetFuncData((i+p)%MainView_GetItemCnt());
						if (regex_search(mvd.path.name,pattern))
						{
							MainView_SetSelectItem((i+p)%MainView_GetItemCnt());
							UpdateMainViewSelection((i+p)%MainView_GetItemCnt(),2,PUI_Event::NowSolvingEvent()->timeStamp);
							break;
						}
					}
				mbl->Close();
			}
		},mbl);
	auto bu=new Button <TextEditLine <MessageBoxLayer*>* > (0,mbl,new PosizeEX_Fa6(1,2,60,20,40,10),PUIT("完成"),[](TextEditLine <MessageBoxLayer*> *&tel)->void{tel->TriggerFunc();},tel);
	SetButtonDUCColor(bu);
	tel->StartTextInput();
}

void PopupFunc_RunInThisDir()
{
	//...
}

enum
{
	RightClickFileNodeFrom_User=0,
	RightClickFileNodeFrom_MainView,
	RightClickFileNodeFrom_MainViewBackground,
	RightClickFileNodeFrom_QuickList,
	RightClickFileNodeFrom_RecentList,
};

void RightClickFileNodes(const PFE_Path &path,int from) 
{
	using OpeData=Triplet <SharedPtr <BaseNodeOperation>,OperationTargets,OperationContext>;
	vector <MenuData<OpeData> > menudata;
	bool div=0;
	OperationTargets tar=InThisSet(from,RightClickFileNodeFrom_QuickList,RightClickFileNodeFrom_RecentList)?OperationTargets(path):OperationTargets();
	OperationContext con(from==RightClickFileNodeFrom_MainView?BaseNodeOperation::From_MainViewMenu:
							(from==RightClickFileNodeFrom_MainViewBackground?BaseNodeOperation::From_MainViewBackground:
							(from==RightClickFileNodeFrom_QuickList?BaseNodeOperation::From_QuickListMenu:
							(from==RightClickFileNodeFrom_RecentList?BaseNodeOperation::From_RecentListMenu:
									BaseNodeOperation::From_User))),CurrentMainView);
	
	auto OperationV=[&](decltype(menudata) &vec,BaseNodeOperation *ope,char hotkey=0)->bool
	{
		int x;
		if (!(x=ope->InvalidFlags(tar,con)))
		{
			div=1;
			vec.push_back(MenuData<OpeData>(ope->Name(),
				[](OpeData &data)
				{
					data.a->Func(data.b,data.c);
				},OpeData(SharedPtr<BaseNodeOperation>(ope),tar,con),hotkey));
			return 1;
		}
//		DD[3]<<"Operation "<<ope->Name()<<" is invalid because of "<<x<<endl;
		delete ope;
		return 0;
	};
	auto Operation=[&](BaseNodeOperation *ope,char hotkey=0)->bool{return OperationV(menudata,ope,hotkey);};
	auto DivideV=[&](decltype(menudata) &vec)
	{
		if (div)
			vec.push_back(MenuData<OpeData>(0)),
			div=0;
	};
	auto Divide=[&](){DivideV(menudata);};
	
	Operation(new Operation_OpenSpecificFile(),'O')||Operation(new Operation_EnterPath(),'O')||Operation(new Operation_OpenMultiFile(),'O');//??
	Divide();
	Operation(new Operation_OpenInNewTab(),'T');
	Divide();
	Operation(new Operation_CopyItemPath(),'P');
	Operation(new Operation_SetPicAsBackground());
	Divide();
	Operation(new Operation_Refresh(),'F');
	Divide();
	Operation(new Operation_RestoreRecycleBin());
	Operation(new Operation_Cut(),'X');
	Operation(new Operation_Copy(),'C');
	Operation(new Operation_Paste(),'V');
	Divide();
	Operation(new Operation_Recycle(),'D');
	Operation(new Operation_Delete());
	Operation(new Operation_Rename(),'M');
	Divide();
	Operation(new Operation_ClearRecycleBin());
	Divide();
	
	decltype(menudata) submenu_new;
	OperationV(submenu_new,new Operation_NewDir(),'F');
	DivideV(submenu_new);
	OperationV(submenu_new,new Operation_NewItem(Operation_New_Item),'0');
	OperationV(submenu_new,new Operation_NewItem(Operation_New_Txt),'1');
	OperationV(submenu_new,new Operation_NewItem(Operation_New_Cpp),'2');
	OperationV(submenu_new,new Operation_NewItem(Operation_New_Docx),'3');
	OperationV(submenu_new,new Operation_NewItem(Operation_New_Pptx),'4');
	if (!submenu_new.empty())
		menudata.push_back(MenuData<OpeData>(submenu_new,PUIT("新建"),200,'W'));
	Divide();
	
	Operation(new Operation_AddQuickList,'Q');
	Operation(new Operation_AddRecentList);
	Divide();
	Operation(new Operation_MoveRemoveInLeftList(Operation_MoveRemoveInLeftList::MoveUp),'A');
	Operation(new Operation_MoveRemoveInLeftList(Operation_MoveRemoveInLeftList::MoveDown),'Z');
	Operation(new Operation_MoveRemoveInLeftList(Operation_MoveRemoveInLeftList::Remove),'D');
	Operation(new Operation_MoveRemoveInLeftList(Operation_MoveRemoveInLeftList::Clear));
	Divide();
	Operation(new Operation_RunAt(Operation_RunAt_Cmd),'S');
	Operation(new Operation_RunAt(Operation_RunAt_PowerShell),'S');
	Operation(new Operation_RunAt(Operation_RunAt_WSLBash),'S');
	Operation(new Operation_SelectInWinExplorer(),'E');
	Operation(new Operation_ShellContextMenu(),'Z');
	Operation(new Operation_Property(),'R');
	
	if (!menudata.empty())
		new Menu1<OpeData>(0,menudata);
}

void MainViewFunc(MainViewData &data,int pos,int click)
{
	if (click==1&&pos!=-1&&PUI_Event::NowSolvingEvent()->type==PUI_Event::Event_MouseEvent&&PUI_Event::NowSolvingEvent()->MouseEvent()->which==PUI_MouseEvent::Mouse_Middle
		&&(CurrentMainView!=CurrentMainView_BVT_RecycleBin||(RecycleBinAllowedOperation&RecycleBinAllowedOperation_ShellContextMenu)))//??
	{
		SelectAllMainViewItem(0);
		SetSelectMainViewItem(pos,1);
		Operation_ShellContextMenu(0);
	}
	else if (click==1)
		UpdateMainViewSelection(pos,0,PUI_Event::NowSolvingEvent()->timeStamp);
	else if (click==2)
	{
		SelectAllMainViewItem(0);
		SetSelectMainViewItem(pos,1);
		if (data.path.type==PFE_PathType_File)
			Operation_OpenSpecificFile().ValidDo(OperationTargets(data.path),OperationContext());//??
		else if (data.path.type==PFE_PathType_VirtualFile)
			Operation_Property(0);
		else SetCurrentPath(data.path,CurrentMainView==CurrentMainView_LVT_SearchResult?SetCurrentPathFrom_Search:SetCurrentPathFrom_MainView);
	}
	else if (click==3)
		if (pos!=-1&&IsMainViewMultiSelected()&&data.CB_Choose->GetOnOff())
			RightClickFileNodes(PFE_Path(),RightClickFileNodeFrom_MainView);
		else
		{
			SelectAllMainViewItem(0);
			SetSelectMainViewItem(pos,1);
			RightClickFileNodes(data.path,pos==-1?RightClickFileNodeFrom_MainViewBackground:RightClickFileNodeFrom_MainView);
		}
}

void TopAddrSecFunc(void *funcdata,AddressSection <PFE_Path> *addrsec,PFE_Path &path,int nowfocus)
{
	if (nowfocus==addrsec->GetSectionCnt())
		return;
	if (nowfocus>=1)
		SetCurrentPath(path,SetCurrentPathFrom_AddrSec);
	else
	{
		vector <PFE_Path> vec;
		if (nowfocus<=-2)
			GetAllFileInDir(addrsec->GetSectionData(-nowfocus-2),vec,GetAllFileInDir_To_AddrSec);
		else
		{
			vec.push_back(PFE_Path_MainPage);
			vec.push_back(PFE_Path_Setting);
			vec.push_back(PFE_Path_RecycleBin);
		}
		int validcnt=0;
		for (const auto &vp:vec)
			if (NotInSet(vp.type,PFE_PathType_File,PFE_PathType_VirtualFile,PFE_PathType_None))
				++validcnt;
		Posize ps(MainWindow->NowPos().x,MainWindow->NowPos().y,300,min3(800,validcnt*26+2,MainWindow->GetWinPS().h));
		ps=MainWindow->GetWinPS().ToOrigin().EnsureIn(ps);
		auto mbl=new MessageBoxLayer(0,"",ps);
		mbl->EnableShowTopAreaColor(0);
		mbl->EnableShowTopAreaX(0);
		mbl->SetClickOutsideReaction(1);
		mbl->SetBackgroundColor(DUC_MessageLayerBackground);
		mbl->SetTopAreaColor(DUC_MessageLayerTopAreaColor);
		auto slv=new SimpleListView <Doublet<PFE_Path,MessageBoxLayer*> > (0,mbl,new PosizeEX_Fa6(2,2,2,2,2,2),
			[](Doublet<PFE_Path,MessageBoxLayer*> &funcdata,int pos,int click)->void
			{
				if (pos==-1) return;
				SetCurrentPath(funcdata.a,SetCurrentPathFrom_AddrSecList);
				funcdata.b->Close();
			});
		for (int i=0;i<=2;++i)
			slv->SetRowColor(i,DUC_ClearThemeColor[i*2]);
		for (auto vp:vec)
			if (vp.type!=PFE_PathType_File)
				slv->PushbackContent(vp.name,{vp,mbl});
	}
}

void LVT_QuickListFunc(LVT_QuickListData &funcdata,int pos,int click)
{
	if (pos==-1) return;
	if (click==1)
		if (funcdata.path.type==PFE_PathType_File)
			Operation_ShellContextMenu().ValidDo(OperationTargets(funcdata.path),OperationContext());
		else SetCurrentPath(funcdata.path,SetCurrentPathFrom_LeftList);
	else if (click==3)
		RightClickFileNodes(funcdata.path,funcdata.ListType?RightClickFileNodeFrom_RecentList:RightClickFileNodeFrom_QuickList);
}

void SetCurrentMainView(int tar,bool fromSetCurrentPathInner=0)
{
	if (tar==CurrentMainView) return;
	bool needrefresh=0;
	switch (tar)
	{
		case CurrentMainView_BVT_0:
		case CurrentMainView_BVT_1:
		case CurrentMainView_BVT_2:
		case CurrentMainView_BVT_3:
		case CurrentMainView_BVT_4:
		case CurrentMainView_BVT_5:
		case CurrentMainView_BVT_6:
		case CurrentMainView_BVT_7:
		case CurrentMainView_BVT_8:
		case CurrentMainView_BVT_9:
		case CurrentMainView_BVT_10:
		case CurrentMainView_BVT_RecycleBin:
		{
			if (Lay_SettingPage!=nullptr)
				OpenSettingPageInLayer(nullptr);
			if (Larlay_Centre==nullptr)
				Larlay_Centre=new LargeLayerWithScrollBar(0,TwinLay_DivideTreeBlock->AreaB(),new PosizeEX_Fa6(2,2,20,5,5,0));
			TT_MainInfo->SetText("");
			if (LVT_MainView!=NULL)
				DelayDeleteToNULL(LVT_MainView);
			if (BVT_MainView==NULL)
				BVT_MainView=new BlockViewTemplate <MainViewData> (0,new PosizeEX_InLarge(Larlay_Centre),MainViewFunc);
			if (InThisSet(tar,CurrentMainView_BVT_0,CurrentMainView_BVT_RecycleBin))
				BVT_MainView->SetEachBlockPosize({5,5,320,80});
			else BVT_MainView->SetEachBlockPosize({5,5,160+(tar-5)*20,190+(tar-5)*20});
			if (!fromSetCurrentPathInner&&InRange(tar,CurrentMainView_BVT_1,CurrentMainView_BVT_10)&&InRange(CurrentMainView,CurrentMainView_BVT_1,CurrentMainView_BVT_10))
			{
				double pos=Larlay_Centre->GetScrollBarPercentY();
				Larlay_Centre->ForceUpdateWidgetTreePosize();//??
				Larlay_Centre->SetViewPort(4,pos);
			}
			if (!fromSetCurrentPathInner&&CurrentMainView==CurrentMainView_LVT_Normal)//??
				needrefresh=1;
			break;
		}
		case CurrentMainView_LVT_Normal:
		case CurrentMainView_LVT_SearchResult:
			if (Lay_SettingPage!=nullptr)
				OpenSettingPageInLayer(nullptr);
			if (Larlay_Centre==nullptr)
				Larlay_Centre=new LargeLayerWithScrollBar(0,TwinLay_DivideTreeBlock->AreaB(),new PosizeEX_Fa6(2,2,20,5,5,0));
			TT_MainInfo->SetText("");
			if (BVT_MainView!=NULL)
				DelayDeleteToNULL(BVT_MainView);
			if (LVT_MainView==NULL)
			{
				LVT_MainView=new ListViewTemplate <MainViewData> (0,new PosizeEX_InLarge(Larlay_Centre),MainViewFunc);
				LVT_MainView->SetRowHeightAndInterval(40,3);
			}
			if (!fromSetCurrentPathInner&&tar==CurrentMainView_LVT_Normal&&InRange(CurrentMainView,CurrentMainView_BVT_0,CurrentMainView_BVT_10))//??
				needrefresh=1;
			break;
		case CurrentMainView_Setting:
			if (Larlay_Centre!=nullptr)
				DelayDeleteToNULL(Larlay_Centre),
				BVT_MainView=nullptr,
				LVT_MainView=nullptr;
			OpenSettingPageInLayer(TwinLay_DivideTreeBlock->AreaB());
			break;
		default:
			DD[2]<<"Bad CurrentMainView,please check the source code!"<<endl;
			return;
	}
	CurrentMainView=tar;
	if (needrefresh)
		RefreshCurrentPath();
	else SetNeedUpdateOperationButton();//??
}

void ChangeMainView(int d)
{
	if (d<0)
		if (InRange(CurrentMainView,CurrentMainView_BVT_2,CurrentMainView_BVT_10))
			SetCurrentMainView(EnsureInRange(CurrentMainView+d,CurrentMainView_BVT_1,CurrentMainView_BVT_10));
		else if (CurrentMainView==CurrentMainView_BVT_0)
			SetCurrentMainView(CurrentMainView_LVT_Normal);
		else DoNothing;
	else if (d>0)
		if (InRange(CurrentMainView,CurrentMainView_BVT_1,CurrentMainView_BVT_9))
			SetCurrentMainView(EnsureInRange(CurrentMainView+d,CurrentMainView_BVT_1,CurrentMainView_BVT_10));
		else if (CurrentMainView==CurrentMainView_LVT_Normal)
			SetCurrentMainView(CurrentMainView_BVT_0);
}

void InitUI_TopArea()
{
	DD[4]<<"InitUI_TopArea"<<endl;
	const int &height=TopAreaHeight;//??
	
	Larlay_TopArea=new LargeLayerWithScrollBar(0,PB_Background,new PosizeEX_Fa6(2,3,0,0,0,height));
	Larlay_TopArea->SetScrollBarWidth(8);
//	Lay_TopArea=new Layer(0,PB_Background,new PosizeEX_Fa6(2,3,0,0,0,height));
//	Lay_TopArea->SetLayerColor(DUC_TopAreaBackground);
	int widthsum=0,gapwidth=2,sidewidth=5;
	widthsum+=sidewidth;
	
	auto bu_setting=new Button <int> (0,Larlay_TopArea->LargeArea(),new PosizeEX_Fa6(3,2,widthsum,60,sidewidth,sidewidth),PUIT("设置"),
		[](int&)
		{
			SetCurrentPath(PFE_Path_Setting,SetCurrentPathFrom_User);//??
		},0);
	SetButtonDUCColor(bu_setting);
	widthsum+=60+gapwidth;
	widthsum+=sidewidth;
	
	new OperationButton(0,Larlay_TopArea->LargeArea(),new PosizeEX_Fa6(3,2,widthsum,60,sidewidth,sidewidth),new Operation_AddQuickList());
	widthsum+=60+gapwidth;
	new OperationButton(0,Larlay_TopArea->LargeArea(),new PosizeEX_Fa6(3,2,widthsum,60,sidewidth,30+sidewidth+gapwidth),new Operation_Copy());
	new OperationButton(0,Larlay_TopArea->LargeArea(),new PosizeEX_Fa6(3,1,widthsum,60,30,sidewidth),new Operation_Cut());
	widthsum+=60+gapwidth;
	new OperationButton(0,Larlay_TopArea->LargeArea(),new PosizeEX_Fa6(3,2,widthsum,60,sidewidth,sidewidth),new Operation_Paste());
	widthsum+=60+gapwidth;
	new OperationButton(0,Larlay_TopArea->LargeArea(),new PosizeEX_Fa6(3,3,widthsum,100,sidewidth,20),new Operation_CopyItemPath());
	widthsum+=100+gapwidth;
	widthsum+=sidewidth;
	
	new OperationButton(0,Larlay_TopArea->LargeArea(),new PosizeEX_Fa6(3,2,widthsum,60,sidewidth,sidewidth),new Operation_Rename());
	widthsum+=60+gapwidth;
	new OperationButton(0,Larlay_TopArea->LargeArea(),new PosizeEX_Fa6(3,2,widthsum,60,sidewidth,30+sidewidth+gapwidth),new Operation_Recycle());
	new OperationButton(0,Larlay_TopArea->LargeArea(),new PosizeEX_Fa6(3,1,widthsum,60,30,sidewidth),new Operation_Delete());
	widthsum+=60+gapwidth;
	auto bu_recyclebin=new Button <int> (0,Larlay_TopArea->LargeArea(),new PosizeEX_Fa6(3,2,widthsum,60,sidewidth,sidewidth),PUIT("回收站"),[](int&){SetCurrentPath(PFE_Path_RecycleBin,SetCurrentPathFrom_User);},0);
	SetButtonDUCColor(bu_recyclebin);
	widthsum+=60+gapwidth;
	widthsum+=sidewidth;
	
	new OperationButton(0,Larlay_TopArea->LargeArea(),new PosizeEX_Fa6(3,2,widthsum,60,sidewidth,sidewidth),new Operation_NewDir());
	widthsum+=60+gapwidth;
	new OperationButton(0,Larlay_TopArea->LargeArea(),new PosizeEX_Fa6(3,3,widthsum,100,sidewidth,20),new Operation_NewItem(Operation_New_Item),PUIT("新建文件"));
	new OperationButton(0,Larlay_TopArea->LargeArea(),new PosizeEX_Fa6(3,3,widthsum,100,sidewidth+20+gapwidth,20),new Operation_NewItem(Operation_New_Txt),PUIT("新建文本文档"));
	new OperationButton(0,Larlay_TopArea->LargeArea(),new PosizeEX_Fa6(3,3,widthsum,100,sidewidth+20+gapwidth+20+gapwidth,20),new Operation_NewItem(Operation_New_Cpp),PUIT("新建C++源代码"));
	widthsum+=100+gapwidth;
	widthsum+=sidewidth;
	
	new OperationButton(0,Larlay_TopArea->LargeArea(),new PosizeEX_Fa6(3,2,widthsum,60,sidewidth,sidewidth),new Operation_Refresh());
	widthsum+=60+gapwidth;
	auto bu_openinexplorer=new Button <int> (0,Larlay_TopArea->LargeArea(),new PosizeEX_Fa6(3,2,widthsum,80,sidewidth,gapwidth+40+sidewidth),"",
		[](int&)->void
		{
			PFE_Path path;
			int pos=MainView_GetCurrentSelectItem();
			if (pos==-1)
				if (MainView_GetItemCnt()==0)
					path=CurrentPath;
				else path=MainView_GetFuncData(0).path;
			else path=MainView_GetFuncData(pos).path;
			if (path.type==PFE_PathType_File||path.type==PFE_PathType_Dir||path.type==PFE_PathType_Volume)
				SelectInWinExplorer(path.str);
		},0);//??
	SetButtonDUCColor(bu_openinexplorer);
	new TinyText(0,bu_openinexplorer,new PosizeEX_MidFa({0,-10,80,20}),PUIT("在资源管"));
	new TinyText(0,bu_openinexplorer,new PosizeEX_MidFa({0,10,80,20}),PUIT("理器中打开"));
	new OperationButton(0,Larlay_TopArea->LargeArea(),new PosizeEX_Fa6(3,1,widthsum,80,40,sidewidth),new Operation_RunAt(Operation_RunAt_Cmd));
	widthsum+=80+gapwidth;
	new OperationButton(0,Larlay_TopArea->LargeArea(),new PosizeEX_Fa6(3,2,widthsum,60,sidewidth,sidewidth),new Operation_Property());
	widthsum+=60+gapwidth;
	widthsum+=sidewidth;
	
	Bu_Sensize=new Button <int> (0,Larlay_TopArea->LargeArea(),new PosizeEX_Fa6(3,3,widthsum,120,sidewidth,60),"",SensizeButtonFunc,0);
	SetButtonDUCColor(Bu_Sensize);
	new TinyText(0,Bu_Sensize,new PosizeEX_MidFa({0,-15,100,30}),PUIT("打开分析器面板"));
	TT_SensizeState=new TinyText(0,Bu_Sensize,new PosizeEX_MidFa({0,15,100,30}),PUIT("当前状态: 未进行"));
	widthsum+=120+gapwidth;
	widthsum+=sidewidth;
	
	DDB_SortMethod=new DropDownButton <int> (0,Larlay_TopArea->LargeArea(),new PosizeEX_Fa6(3,3,widthsum,160,sidewidth,30),"",[](int&,int pos){SetCurrentSortMode(pos,PathSortReversed,1);});
	DDB_SortMethod->SetBackgroundColor(DUC_ClearBackground0);
	for (int i=0;i<=2;++i)
		DDB_SortMethod->SetListColor(i,DUC_ClearThemeColor[i*2]);
	DDB_SortMethod->PushbackChoiceData(PUIT("排序方式: 常规"),PathSortFunc_Normal);
	DDB_SortMethod->PushbackChoiceData(PUIT("排序方式: 无"),PathSortFunc_None);
	DDB_SortMethod->PushbackChoiceData(PUIT("排序方式: 字典序"),PathSortFunc_LexicographicalOrder);
	DDB_SortMethod->PushbackChoiceData(PUIT("排序方式: 大小"),PathSortFunc_FileSize);
	DDB_SortMethod->PushbackChoiceData(PUIT("排序方式: 大小变化"),PathSortFunc_FileSizeDelta);
	DDB_SortMethod->PushbackChoiceData(PUIT("排序方式: 后缀名"),PathSortFunc_Aftername);
	DDB_SortMethod->PushbackChoiceData(PUIT("排序方式: 修改日期"),PathSortFunc_WriteTime);
	DDB_SortMethod->PushbackChoiceData(PUIT("排序方式: 创建日期"),PathSortFunc_CreateTime);
	DDB_SortMethod->SetSelectChoice(0,0);
	Bu_ReversedSort=new ButtonI(0,Larlay_TopArea->LargeArea(),new PosizeEX_Fa6(3,3,widthsum+160,30,sidewidth,30),PathSortReversed?PUIT("↑"):PUIT("↓"),[](int&){SetCurrentSortMode(PathSortMode,!PathSortReversed,1);},0);
	
	new CheckBox <int> (0,Larlay_TopArea->LargeArea(),new PosizeEX_Fa6(3,3,widthsum,20,sidewidth+gapwidth+40,20),0,[](int&,bool onoff)->void{SearchTextFileContent=onoff;},0);
	new TinyText(0,Larlay_TopArea->LargeArea(),new PosizeEX_Fa6(3,3,widthsum+25,135,sidewidth+gapwidth+40,20),PUIT("搜索文本内容"),-1);
	
	widthsum+=190;
	widthsum+=sidewidth;
	
//	new ButtonI(0,Larlay_TopArea->LargeArea(),new PosizeEX_Fa6(3,3,widthsum,120,sidewidth,30),"RollPictures(Test)",[](int&)
//	{
//		if (PFE_Cfg("RollPicturePath")=="") return;
//		DD[3]<<"TestingFunction: OpenRollPicture "<<CurrentPath.str<<endl;
//		ShellExecuteW(0,L"open",Utf8ToUnicode(PFE_Cfg("RollPicturePath")).c_str(),Utf8ToUnicode("\""+CurrentPath.str+"\"").c_str(),Utf8ToUnicode(CurrentPath.str).c_str(),SW_SHOWNORMAL);
//	},0);
//	widthsum+=120;
//	widthsum+=sidewidth;
	
	Larlay_TopArea->SetMinLargeAreaSize(widthsum,height);
	DD[5]<<"InitUI_TopArea"<<endl;
}

void InitUI()
{
	DD[4]<<"InitUI"<<endl;
	string s;
	
	if (strISull(s=PFE_Cfg("CurrentThemeColorCode")))
	{
		CurrentThemeColorCode=strTOll(s);
		ThemeColor=PUI_ThemeColor(CurrentThemeColorCode);
	}
	RegisterDynamicUserColor();
	
	PB_Background=new PictureBox <int> (0,MainWindow->BackGroundLayer(),new PosizeEX_Fa6_Full);
	PB_Background->SetBackgroundColor(RGBA_NONE);
	(new Layer(0,PB_Background,new PosizeEX_Fa6_Full))->SetLayerColor(DUC_BackgroundCover);
	
	if (strISull(s=PFE_Cfg("WidgetsOpacityPercent")))
		WidgetsOpacityPercent=strTOll(s);
	if (strISull(s=PFE_Cfg("BackgroundPictureBoxMode")))
		BackgroundPictureBoxMode=strTOll(s);
	if ((s=PFE_Cfg("BackgroundPicture"))!="")
		SetBackgroundPic(s,InitBackgroundPictureLoadMode==0?0:1);

	if (EnableBackgroundCoverColor=PFE_Cfg("EnableBackgroundCoverColor")=="1")
		if (PFE_Cfg["BackgroundCoverColor"].size()==4)
			BackgroundCoverColor=RGBA(strTOint(PFE_Cfg["BackgroundCoverColor"][0]),strTOint(PFE_Cfg["BackgroundCoverColor"][1]),strTOint(PFE_Cfg["BackgroundCoverColor"][2]),strTOint(PFE_Cfg["BackgroundCoverColor"][3]));
	
	InitUI_TopArea();
	
	Lay_AddrSecArea=new Layer(0,PB_Background,new PosizeEX_Fa6(2,3,0,0,TopAreaHeight+5,30));
//	Lay_AddrSecArea->SetLayerColor(DUC_LeftAreaBackground);
	
	Lay_AddrSecAreaCenter=new LayerForAddrSecArea(0,Lay_AddrSecArea,new PosizeEX_Fa6(2,2,100,260,0,0));
	
	ProBar_Top=new ProgressBar(0,Lay_AddrSecAreaCenter,new PosizeEX_Fa6_Full);
	ProBar_Top->SetBackgroundColor(DUC_ClearThemeColor[0]);
	ProBar_Top->SetBarColor(DUC_ClearThemeColor[4]);
	ProBar_Top->SetFullColor(DUC_ClearThemeColor[7]);

	AddrSec_TopAddress=new AddressSection <PFE_Path> (0,Lay_AddrSecAreaCenter,new PosizeEX_Fa6_Full,TopAddrSecFunc);
	for (int i=0;i<=5;++i)
		AddrSec_TopAddress->SetBackgroundColor(i,DUC_ClearThemeColor[i%3*2+1+int(i>=3)]);
	Lay_AddrSecAreaCenter->SetAddrSec(AddrSec_TopAddress);
	
	Bu_GoBack=new Button <int> (0,Lay_AddrSecArea,new PosizeEX_Fa6(3,2,5,30,0,0),"<",
		[](int &funcdata)->void
		{
			if (!CurrentPathContext->PathHistory.empty())
				SetCurrentPath(CurrentPathContext->PathHistory.back().path,SetCurrentPathFrom_GoBack);
		},NULL);
	SetButtonDUCColor(Bu_GoBack);
	Bu_GoForward=new Button<int>(0,Lay_AddrSecArea,new PosizeEX_Fa6(3,2,35,30,0,0),">",
		[](int &funcdata)->void
		{
			if (!CurrentPathContext->PopHistory.empty())
				SetCurrentPath(CurrentPathContext->PopHistory.back().path,SetCurrentPathFrom_GoForward);
		},NULL);
	SetButtonDUCColor(Bu_GoForward);
	Bu_GoUp=new Button<int>(0,Lay_AddrSecArea,new PosizeEX_Fa6(3,2,65,30,0,0),"^",
		[](int &funcdata)->void
		{
			if (AddrSec_TopAddress->GetSectionCnt()>=2)
				SetCurrentPath(AddrSec_TopAddress->GetSectionData(AddrSec_TopAddress->GetSectionCnt()-2),SetCurrentPathFrom_GoUp);
		},NULL);
	SetButtonDUCColor(Bu_GoUp);
	TEL_SearchFile=new TextEditLine <int> (0,Lay_AddrSecArea,new PosizeEX_Fa6(1,2,240,10,0,0),
		[](int &funcdata,const stringUTF8 &strUtf8,TextEditLine <int> *tel,bool isenter)->void
		{
			if (!isenter) return;
			string str=strUtf8.cppString();
			if (CurrentPath.type!=PFE_PathType_File&&CurrentPath.str!=""&&DeleteSideBlank(str)!="")//??
			{
				SetCurrentPath(PFE_Path(PFE_PathType_Search,CurrentPath.str,PUIT("在  ")+CurrentPath.str+PUIT("  中搜索  ")+str),SetCurrentPathFrom_Search);
				StartSearchThread(CurrentPath,str);
				TT_MainInfo->SetText(PUIT("搜索中，已费时")+llTOstr(SDL_GetTicks()-SearchStartTick)+PUIT("毫秒..."));
				LVT_MainView->ClearListContent();
			}
		},0);
	TEL_SearchFile->SetBackgroundColor(0,DUC_ClearBackground0);
	
	TwinLay_DivideTreeBlock=new TwinLayerWithDivideLine(0,PB_Background,new PosizeEX_Fa6(2,2,0,0,TopAreaHeight+40,20),1,0.14);
	TwinLay_DivideTreeBlock->SetDivideLineMode(1,50,-0.3);
	
//	if (PFE_Cfg("RememberMainViewDivideLinePos")=="1"&&strISull(s=PFE_Cfg("MainViewDivideLinePosPixels")))
//		TwinLay_DivideTreeBlock->SetDivideLinePosition((strTOll(s)+TwinLay_DivideTreeBlock->GetDivideLineShowWidth()/2)*1.0/TwinLay_DivideTreeBlock->GetrPS().w);
	
//	TwinLay_DivideTreeBlock->SetAreaAColor(DUC_LeftAreaBackground);
//	TwinLay_DivideTreeBlock->SetAreaBColor(DUC_MainAreaBackground);
	
//	STV_sideTreeView=new SimpleTreeView1 <SharedFileNodePtr> (0,Lyr_DivideTreeBlock->AreaA(),new PosizeEX_Fa6(2,2,0,0,0,0),func_sideSTV);
//	SimpleTreeView1<SharedFileNodePtr>::TreeViewData tvd0(FND_thisPC().name,FND_thisPC,1);
//	STV_sideTreeView->PushbackNode(-1,tvd0);
	
	Larlay_Centre=new LargeLayerWithScrollBar(0,TwinLay_DivideTreeBlock->AreaB(),new PosizeEX_Fa6(2,2,20,5,5,0));
	
	BVT_MainView=new BlockViewTemplate <MainViewData> (0,new PosizeEX_InLarge(Larlay_Centre),MainViewFunc);
	BVT_MainView->SetEachBlockPosize({5,5,320,80});
	
	Larlay_LeftView=new LargeLayerWithScrollBar(0,TwinLay_DivideTreeBlock->AreaA(),new PosizeEX_Fa6(2,2,0,0,0,0));
	
	Layer *Lay_QuickListText=new Layer(0,Larlay_LeftView->LargeArea(),new PosizeEX_InLarge(Larlay_LeftView,0,0,30));
	Lay_QuickListText->SetLayerColor(DUC_ClearBackground0);
	new TinyText(0,Lay_QuickListText,new PosizeEX_Fa6(2,2,20,10,0,0),PUIT("快速访问"),-1,ThemeColorM[6]);
	LVT_QuickList=new ListViewTemplate <LVT_QuickListData> (0,new PosizeEX_InLarge(Larlay_LeftView),LVT_QuickListFunc);
	for (int i=0;i<=2;++i)
		LVT_QuickList->SetDefaultRowColor(i,DUC_QuictListColor[i]);
	
	Layer *Lay_RecentListText=new Layer(0,Larlay_LeftView->LargeArea(),new PosizeEX_InLarge(Larlay_LeftView,10,0,30));
	Lay_RecentListText->SetLayerColor(DUC_ClearBackground0);
	new TinyText(0,Lay_RecentListText,new PosizeEX_Fa6(2,2,20,10,0,0),PUIT("近期访问"),-1,ThemeColorM[5]);
	int AreaUnderLVTHeigthWithoutGap=0;
	LVT_RecentList=new ListViewTemplate <LVT_QuickListData> (0,new PosizeEX_InLarge(Larlay_LeftView,0,(AreaUnderLVTHeigthWithoutGap==0?1:-AreaUnderLVTHeigthWithoutGap-20)),LVT_QuickListFunc);
	for (int i=0;i<=2;++i)
		LVT_RecentList->SetDefaultRowColor(i,DUC_RecentListColor[i]);
	
//	Layer *Lay_AreaUnderLVT=new Layer(0,Larlay_LeftView->LargeArea(),new PosizeEX_InLarge(Larlay_LeftView,10,0,AreaUnderLVTHeigthWithoutGap+10));
//	auto Bu_Setting=new ButtonI(0,Lay_AreaUnderLVT,new PosizeEX_Fa6(2,2,0,0,0,10),PUIT("设置"),[](int&)
//	{
//		SetCurrentPath(PFE_Path_Setting,SetCurrentPathFrom_User);
//	},0);
//	for (int i=0;i<=2;++i)
//		Bu_Setting->SetButtonColor(i,DUC_ClearThemeColor[i*2]);
	
	TT_MainInfo=new TinyText(0,PB_Background,new PosizeEX_Fa6(2,1,30,30,20,1),"",-1);
	
//	ProBar_CurrentVolumeSize=new ProgressBar(0,new PB_Background,new PosizeEX_Fa6(1,1,200,50,20,0));
//	TT_CurrentVolumeSize=new TinyText(0,ProBar_CurrentVolumeSize,new PosizeEX_Fa6(2,2,0,0,0,0),"");
//	
//	Bu_ChangeView1=new Button <int> (0,PB_Background,new PosizeEX_Fa6(1,1,20,10,20,0),"+",NULL,0);
//	Bu_ChangeView2=new Button <int> (0,PB_Background,new PosizeEX_Fa6(1,1,20,30,20,0),"-",NULL,0);

	using GSL=GestureSenseLayer <int>;
	GSL_GlobalGesture=new GestureSenseLayerI(0,PB_Background,new PosizeEX_Fa6(2,2,0,0,0,0),GSL::Gb_SlideR<<2|GSL::Gb_SlideL<<2|GSL::Gb_SlideD<<2|GSL::Gb_SlideD<<3|GSL::Gb_SlideU<<3,
		[](int&,const GestureSenseLayerI::GestureData &data,const PUI_TouchEvent *touch)
		{
			if (data.dist<=50) return;
			static unsigned int last3fingerTimestamp=0;
			if (data.stat==1&&data.count==1)
				switch (data.gesture)
				{
//					case GSL::G_SlideL:
//					case GSL::G_SlideR:
//						if (CurrentTabMode!=TabMode_None&&TM_Tab!=nullptr&&TM_Tab->CurrentTabDataLayer()!=nullptr)
//							TM_Tab->SwitchTab((TM_Tab->CurrentTabDataLayer()->GetIndex()+((CurrentTabMode==TabMode_Top)^(data.gesture==GSL::G_SlideL)?-1:1)+TM_Tab->GetTabCnt())%TM_Tab->GetTabCnt());
//						break;
				}
			else if (data.stat==1&&data.count==2)
				switch (data.gesture)
				{
					case GSL::G_SlideR:	Bu_GoBack->TriggerFunc();	break;
					case GSL::G_SlideL:	Bu_GoForward->TriggerFunc();	break;
					case GSL::G_SlideD:	Bu_GoUp->TriggerFunc();	break;
				}
			else if (data.stat!=-1&&data.count==3&&touch->timeStamp-last3fingerTimestamp>=200&&touch->timeStamp-PUI_TouchEvent::Finger::gestureStartTime>=200)
			{
				last3fingerTimestamp=touch->timeStamp;
				switch (data.gesture)
				{
					case GSL::G_SlideD:	ChangeMainView(-1);	break;
					case GSL::G_SlideU: ChangeMainView(1);	break;
//					case GSL::G_SlideL:	case GSL::G_SlideR:
//						if (CurrentTabMode!=TabMode_None&&TM_Tab!=nullptr&&TM_Tab->CurrentTabDataLayer()!=nullptr)
//							TM_Tab->SwitchTab((TM_Tab->CurrentTabDataLayer()->GetIndex()+((CurrentTabMode==TabMode_Top)^(data.gesture==GSL::G_SlideL)?1:-1)+TM_Tab->GetTabCnt())%TM_Tab->GetTabCnt());
//						break;
				}
			}
		},0);

	if (HaveBackgroundPic)
		SetUIWidgetsOpacity(WidgetsOpacityPercent);
	else SetUIWidgetsOpacity(0);
	
	SetLeftListItem(0,PFE_Path_MainPage,0);
	{
		auto &vec=PFE_Cfg["QuickList"];
		for (int i=3,j=1;i<vec.size();i+=4,++j)
			SetLeftListItem(j,PFE_Path(strTOll(vec[i-3]),vec[i-2],vec[i-1],strTOll(vec[i])),0);
	}
	
	InitTabView();
	DD[5]<<"InitUI"<<endl;
}

int PAL_FileExplorer_Init(int argc,char **argv)
{
	string s;
	SetCmdUTF8AndPrintInfo(ProgramName,ProgramVersion,"qianpinyi");
	ProgramPath=GetPreviousBeforeBackSlash(argv[0]);
	bool DataHere=IsFileExsit(ProgramPath+"\\Data\\PAL_FileExplorer_Config.txt",0);
	
	if (!DataHere)
	{
		string AppDataLocalPath=PAL_Platform::GetAppDataLocalDirectoryPath();
		if (AppDataLocalPath!="")
			UserDataPath=ProgramDataPath=AppDataLocalPath+"\\PAL_FileExplorer";
		else UserDataPath=ProgramDataPath=ProgramPath;
	}
	else UserDataPath=ProgramDataPath=ProgramPath;
	ProgramDataPath+="\\Data";
	UserDataPath+="\\Userdata";
	if (!IsFileExsit(ProgramDataPath,1))
		if (CreateDirectoryR(ProgramDataPath))
			return cerr<<"Error: Failed to create data directory!"<<endl,4;
	if (!IsFileExsit(UserDataPath,1))
		if (CreateDirectoryR(UserDataPath))
			return cerr<<"Error: Failed to create userdata directory!"<<endl,4;
	
	DD.SetLOGFile(ProgramDataPath+"\\PAL_FileExplorer_Log.txt");
	DD%DebugOut_Channel::DebugOut_CERR_LOG;
	DD.SetDebugType(4,"Function Do");
	DD.SetDebugType(5,"Function OK");
	DD[4]<<"PAL_FileExplorer_Init"<<endl;
	
	if (!DataHere)
		if (!IsFileExsit(ProgramDataPath+"\\PAL_FileExplorer_Config.txt",0))
			if (!CopyFileW(Utf8ToUnicode(ProgramPath+"\\InitConfig.txt").c_str(),Utf8ToUnicode(ProgramDataPath+"\\PAL_FileExplorer_Config.txt").c_str(),1))
				return DD[2]<<"Failed to create config file!"<<endl,5;
	SetCurrentDirectoryW(DeleteEndBlank(Utf8ToUnicode(ProgramDataPath)).c_str());
	PFE_Cfg.SetCfgFile("PAL_FileExplorer_Config.txt");//???It is not good,but fstream don't accept Unicode...
	if (PFE_Cfg.Read()!=0)
		return DD[2]<<"Failed to read config file. Error code:"<<PFE_Cfg.GetLastStat()<<endl,1;
	else DD[0]<<"Read config file OK."<<endl;
	
	if (argc>=2)
	{
		if (InThisSet(string(argv[1]),"-n","/n"))
		{
			DD[0]<<"Start with opened path tab."<<endl;
			for (int i=2;i<argc;++i)
				StartWithOpenedPathTab.push_back(argv[i]);
		}
	}
	
	if (PFE_Cfg("DisableRenderLinearScale")!="1")
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"linear");
	
	EnableAutoSelectLastPath=PFE_Cfg("EnableAutoSelectLastPath")!="0";
	ShowAfternameInName=PFE_Cfg("ShowAfternameInName")!="0";
	ShowHiddenFile=PFE_Cfg("ShowHiddenFile")!="0";
	
	if (strISull(s=PFE_Cfg("ShowFullPathInWindowTitle")))
		ShowFullPathInWindowTitle=strTOll(s);
	
	unsigned int winFlag=PUI_Window::PUI_FLAG_RESIZEABLE;
	if (PFE_Cfg("DisableRendererAccelerate")=="1")
		winFlag|=PUI_Window::PUI_FLAG_SOFTWARE;
	
	if (strISull(s=PFE_Cfg("PreferredRenderer")))
		PUI_SetPreferredRenderer(strTOll(s));

	Posize lastWindowPosize=ZERO_POSIZE;
	bool lastWindowMaximized=0;
	if (PFE_Cfg("EnableRememberLastWindowPosize")=="1")
	{
		auto &vec=PFE_Cfg["LastWindowPosize"];
		if (vec.size()==4)
			lastWindowPosize=Posize(strTOll(vec[0]),strTOll(vec[1]),strTOll(vec[2]),strTOll(vec[3]));
		lastWindowMaximized=PFE_Cfg("LastWindowIsMaximized")=="1";
	}
	
	if (strISull(s=PFE_Cfg("EnableRecentFileHighLight")))
		EnableRecentFileHighLight=strTOll(s);
	if (strISull(s=PFE_Cfg("SwitchToPictureViewPercent")))
		SwitchToPictureViewPercent=strTOll(s);
	if (strISull(s=PFE_Cfg("InitBackgroundPictureLoadMode"))) 
		InitBackgroundPictureLoadMode=strTOll(s);
	if (strISull(s=PFE_Cfg("SynchronizeWithSystemClipboard")))
		SynchronizeWithSystemClipboard=strTOll(s);
	if (strISull(s=PFE_Cfg("RingWhenAllFileOperationIsCompleted")))
		RingWhenAllFileOperationIsCompleted=strTOll(s);
	if (strISull(s=PFE_Cfg("RecycleBinAllowedOperation")))
		RecycleBinAllowedOperation=strTOll(s);
	if (strISull(s=PFE_Cfg("ShowRealDeleteInRigthClickMenu")))
		ShowRealDeleteInRigthClickMenu=strTOll(s);
	
	DD[4]<<"PAL_GUI_Init"<<endl;
	PAL_GUI_Init(lastWindowPosize==ZERO_POSIZE?Posize(PUI_Window::PUI_WINPS_UNDEFINE,PUI_Window::PUI_WINPS_UNDEFINE,1600,900):lastWindowPosize,ProgramNameVersion,winFlag);
	PUI_DD.SetDebugTypePosOnOff(0,0);
	DD[5]<<"PAL_GUI_Init"<<endl;
	
	if (lastWindowMaximized)
		MainWindow->MaximizeWindow();
	
	TurnOnOffIMEWindow(0,MainWindow,1);//??
	
	for (const auto &vp:PFE_Cfg["AcceptedPictureFormat"])
		if (vp!="")
			AcceptedPictureFormat.insert(vp);
	PicFileManager.SetPicFileAftername(AcceptedPictureFormat);
	
	for (const auto &vp:PFE_Cfg["SearchFileContentTarget"])
		if (vp!="")
			SearchFileContentTarget.insert(vp);
	
	if (GetItemIconInit())
		return DD[2]<<"Failed to init GetItemIcon submodule!"<<endl,3;
	
	InitMonitorChangeThread();
	
	UserEvent_SearchFileResult=PUI_Event::RegisterEvent(1);
	UserEvent_Sensize=PUI_Event::RegisterEvent(1);
	UserEvent_SensizeLoad=PUI_Event::RegisterEvent(1);
	UserEvent_SensizeSave=PUI_Event::RegisterEvent(1);
	
	if (strISull(s=PFE_Cfg("RecentListLRULimit")))
		RecentListLRULimit=strTOll(s);
	LRU_RecentList.SetSizeLimit(RecentListLRULimit);
	LRU_RecentList.SetKickoutCallback([](const PFE_Path &path,const int&)->void
	{
		int p=LVT_RecentList->Find(LVT_QuickListData(path,0));
		if (p!=-1) LVT_RecentList->DeleteListContent(p);
	});
	
	InitUI();
	
	DD[5]<<"PAL_FileExplorer_Init"<<endl;
	return 0;
}

int PAL_FileExplorer_Quit()
{
	DD[4]<<"PAL_FileExplorer_Quit"<<endl;
	
	{
		auto &vec=PFE_Cfg["QuickList"];
		vec.clear();
		for (int i=1;i<LVT_QuickList->GetListCnt();++i)
		{
			LVT_QuickListData &data=LVT_QuickList->GetFuncData(i);
			vec.push_back(llTOstr(data.path.type));
			vec.push_back(data.path.str);
			vec.push_back(data.path.name);
			vec.push_back(llTOstr(data.path.code));
		}
	}

	SavePathHistoryData();
	
	GetItemIconQuit();
	
	CurrentSensize.StopSensize();
	LoadedSensize.StopSensize();
	
	QuitMonitorChangeThread();
	
	StopSearchThread();
	
	FileOperationQuit();
	
//	if (PFE_Cfg("RememberMainViewDivideLinePos")=="1")
//		PFE_Cfg("MainViewDivideLinePosPixels")=TwinLay_DivideTreeBlock->AreaA()->GetrPS().w;
	
	PFE_Cfg("WidgetsOpacityPercent")=llTOstr(WidgetsOpacityPercent);
	PFE_Cfg("CurrentThemeColorCode")=llTOstr(ThemeColor.CurrentThemeColor);
	PFE_Cfg("EnableBackgroundCoverColor")=EnableBackgroundCoverColor?"1":"0"; 
	{
		auto &vec=PFE_Cfg["BackgroundCoverColor"];
		vec.clear();
		vec.push_back(llTOstr(BackgroundCoverColor.r));
		vec.push_back(llTOstr(BackgroundCoverColor.g));
		vec.push_back(llTOstr(BackgroundCoverColor.b));
		vec.push_back(llTOstr(BackgroundCoverColor.a));
	}
	
	auto &lastWindowPosize=PFE_Cfg["LastWindowPosize"];
	lastWindowPosize.clear();
	if (PFE_Cfg("EnableRememberLastWindowPosize")=="1")
	{
		lastWindowPosize.push_back(llTOstr(MainWindow->GetWinPS().x));
		lastWindowPosize.push_back(llTOstr(MainWindow->GetWinPS().y));
		lastWindowPosize.push_back(llTOstr(MainWindow->GetWinPS().w));
		lastWindowPosize.push_back(llTOstr(MainWindow->GetWinPS().h));
		PFE_Cfg("LastWindowIsMaximized")=MainWindow->GetSDLWindowFlags()&SDL_WINDOW_MAXIMIZED?"1":"0";
	}
	else PFE_Cfg("LastWindowIsMaximized")="";

	if (PFE_Cfg.Write(1))
		DD[2]<<"Failed to write config file! Error code:"<<PFE_Cfg.GetLastStat()<<endl;
	else DD[0]<<"Write config file OK."<<endl;

	DD[4]<<"PAL_GUI_Quit"<<endl;
	CheckErrorAndDDReturn(PAL_GUI_Quit(),1);
	DD[5]<<"PAL_GUI_Quit"<<endl;
	
	DD[5]<<"PAL_FileExplorer_Quit"<<endl;
	return 0;
}

bool EventLoopCallBack(const PUI_Event *event,bool stage,int &quitflag)
{
	if (stage==0) switch (event->type)
	{
		case PUI_Event::Event_WheelEvent:
		{
			if ((SDL_GetModState()&KMOD_CTRL))
				return ChangeMainView(event->WheelEvent()->dy),1;
			else return 0;
		}
		default:
			if (event->type==UserEvent_SearchFileResult)//???
				if (event->UserEvent()->code==1)
				{
					WarningMessageButton(SetSystemBeep_Info,PUIT("提示！"),PUIT("文件搜索完成！共搜索到 "+llTOstr((int)LastSearchResult.size()-1)+"个匹配项."));
					TT_MainInfo->SetText(PUIT("搜索完成，共费时 "+llTOstr(SDL_GetTicks()-SearchStartTick)+" 毫秒，共 "+llTOstr((int)LastSearchResult.size()-1)+" 个匹配项."));
					if (LastSearchResult.size()==1)	
						if (TT_InfoOfDir==NULL)
							TT_InfoOfDir=new TinyText(0,TwinLay_DivideTreeBlock->AreaB(),new PosizeEX_Fa6(2,3,20,20,50,20),PUIT("无匹配项."));
						else DD[1]<<"There may exsit error in line["<<__LINE__<<"]"<<endl;
					if (SearchedBufferCount!=0)
						DD[1]<<"SearchedBufferCount is not 0,maybe there is something wrong in logic!"<<endl;
				}
				else if (event->UserEvent()->code==2)
					TT_MainInfo->SetText(PUIT("搜索中，已费时")+llTOstr(SDL_GetTicks()-SearchStartTick)+PUIT("毫秒..."));
				else
				{
					auto *vec=(vector <PFE_Path>*)event->UserEvent()->data1;
					if (SearchResultCountToReject>0)
						--SearchResultCountToReject;
					else
					{
						if (CurrentMainView==CurrentMainView_LVT_SearchResult)
							for (auto vp:*vec)
							{
								LastSearchResult.push_back(vp);
								PushbackMainViewContent(vp);
							}
						SearchedBufferCount--;
					}
					TT_MainInfo->SetText(PUIT("搜索中，已费时")+llTOstr(SDL_GetTicks()-SearchStartTick)+PUIT("毫秒..."));
					delete vec;
				}
			else if (InThisSet(event->type,UserEvent_Sensize,UserEvent_SensizeLoad,UserEvent_SensizeSave))
				if (event->UserEvent()->code==0)
				{
					DD[0]<<"Sensize"<<(event->type==UserEvent_SensizeLoad?" load":(event->type==UserEvent_SensizeSave?" save":""))<<" OK."<<endl;
					TT_SensizeState->SetText(PUIT("当前状态: 已完成"));
					TT_SensizeState->SetTextColor(ThemeColorM[7]);
					if (event->type==UserEvent_SensizeSave&&SensizePanel.MBL_SensizePanel!=NULL)
						if (SensizePanel.SLV_SavedList->Find(CurrentSensize.GetLSPath())==-1)
							SensizePanel.SLV_SavedList->PushbackContent(GetWithOutAftername(GetLastAfterBackSlash(CurrentSensize.GetLSPath())),CurrentSensize.GetLSPath());
					if (InRange(CurrentMainView,CurrentMainView_BVT_0,CurrentMainView_BVT_10)||CurrentMainView==CurrentMainView_LVT_Normal)//??
						SetCurrentSortMode(PathSortFunc_FileSize,0,1);
				}
				else if (event->UserEvent()->code!=SensizeData::ERR_Canceled)
				{
					WarningMessageButton(SetSystemBeep_Error,PUIT("错误!"),PUIT(string(event->type==UserEvent_SensizeLoad?"载入分析结果":(event->type==UserEvent_SensizeSave?"保存分析结果":"分析运行"))+"失败! 错误码: "+llTOstr(event->UserEvent()->code)));
					if (event->type!=UserEvent_SensizeSave)
						TT_SensizeState->SetText(PUIT("当前状态: 执行失败")),
						TT_SensizeState->SetTextColor({255,26,0,255});
				}
				else DoNothing;
			else if (event->type==UserEvent_AutoRefresh)
				RefreshCurrentPath();
			break;
	}
	else switch (event->type)
	{
		case PUI_Event::Event_KeyEvent:
		{
			PUI_KeyEvent *keyevent=event->KeyEvent();
			if (keyevent->IsDownOrHold()&&keyevent->win->GetKeyboardInputWg()==NULL)
				if ((keyevent->mod&KMOD_CTRL)&&(keyevent->mod&KMOD_SHIFT))
				{
					switch (keyevent->keyCode)
					{
						case SDLK_n:	Operation_NewDir(0);	break;//???
						case SDLK_TAB:
							if (CurrentTabMode!=TabMode_None&&TM_Tab!=nullptr&&TM_Tab->CurrentTabDataLayer()!=nullptr)
								TM_Tab->SwitchTab((TM_Tab->CurrentTabDataLayer()->GetIndex()+(CurrentTabMode==TabMode_Top?TM_Tab->GetTabCnt()-1:1))%TM_Tab->GetTabCnt());
							break;
					}
				}
				else if ((keyevent->mod&KMOD_CTRL)&&(keyevent->mod&KMOD_ALT))
				{
					switch (keyevent->keyCode)
					{
						case SDLK_F1:
							TerminateProcess(GetCurrentProcess(),1);
					}
				}
				else if (keyevent->mod&KMOD_CTRL)
				{
					switch (keyevent->keyCode)
					{
						case SDLK_F1://Force quit
							exit(1);
						case SDLK_F6://For test
						{
							++CurrentThemeColorCode;
							if (CurrentThemeColorCode==PUI_ThemeColor::PUI_ThemeColor_UserDefined)
								CurrentThemeColorCode=PUI_ThemeColor::PUI_ThemeColor_Blue;
							ThemeColor=PUI_ThemeColor(CurrentThemeColorCode);
							SetUIWidgetsOpacity(WidgetsOpacityPercent);
							break;
						}
						case SDLK_F8://For test
							BVT_MainView->SetEnabled(!BVT_MainView->GetEnabled());
							break;
						case SDLK_MINUS:	ChangeMainView(-1);	break;
						case SDLK_EQUALS:	case SDLK_PLUS:	ChangeMainView(1);	break;
						case SDLK_c:	Operation_Copy(0);	break;
						case SDLK_x:	Operation_Cut(0);	break;
						case SDLK_v:	Operation_Paste(0);	break;
						case SDLK_d:	Operation_Recycle(0);	break;
						case SDLK_n:	Operation_NewDir(0);	break;
						case SDLK_m:	Operation_New_Item.ValidDo(OperationTargets(),OperationContext());	break;
						case SDLK_r:	Operation_Refresh(0);	break;
						case SDLK_a:	SelectAllMainViewItem(1);	break;
						case SDLK_t:
							if (CurrentTabMode!=TabMode_None&&TM_Tab!=nullptr)
								TM_Tab->AddTab(1e9,new PathContext(PFE_Path_MainPage),PFE_Path_MainPage.name);
							break;
						case SDLK_w:
							if (CurrentTabMode!=TabMode_None&&TM_Tab!=nullptr&&TM_Tab->CurrentTabDataLayer()!=nullptr)
								TM_Tab->CloseTab(TM_Tab->CurrentTabDataLayer()->GetIndex());
							break;
						case SDLK_TAB:
							if (CurrentTabMode!=TabMode_None&&TM_Tab!=nullptr&&TM_Tab->CurrentTabDataLayer()!=nullptr)
								TM_Tab->SwitchTab((TM_Tab->CurrentTabDataLayer()->GetIndex()+(CurrentTabMode==TabMode_Top?1:TM_Tab->GetTabCnt()-1))%TM_Tab->GetTabCnt());
							break;
						case SDLK_SPACE:
							UpdateMainViewSelection(MainView_GetCurrentSelectItem(),1,keyevent->timeStamp);
							break;
						case SDLK_UP:
						case SDLK_DOWN:
						case SDLK_LEFT:
						case SDLK_RIGHT:
							if (MainView_GetItemCnt()!=0)
								if (int p=MainView_GetCurrentSelectItem();p!=-1)
								{
									if (InRange(CurrentMainView,CurrentMainView_BVT_0,CurrentMainView_BVT_10)||InThisSet(CurrentMainView,CurrentMainView_BVT_RecycleBin))
										BVT_MainView->SetSelectBlock(p=EnsureInRange(p+(keyevent->keyCode==SDLK_UP?-BVT_MainView->GetEachLineBlocks()
																					:(keyevent->keyCode==SDLK_DOWN?BVT_MainView->GetEachLineBlocks()
																					:(keyevent->keyCode==SDLK_LEFT?-1:1))),0,BVT_MainView->GetBlockCnt()-1));
									else LVT_MainView->SetSelectLine(p=EnsureInRange(p+(keyevent->keyCode==SDLK_UP||keyevent->keyCode==SDLK_LEFT?-1:1),0,LVT_MainView->GetListCnt()-1));
								}
								else MainView_SetSelectItem(0);
							break;
					}
				}
				else if (keyevent->mod&KMOD_ALT)
				{
					switch (keyevent->keyCode)
					{
						case SDLK_LEFT:	Bu_GoBack->TriggerFunc();	break;
						case SDLK_RIGHT:	Bu_GoForward->TriggerFunc();	break;
						case SDLK_UP:	Bu_GoUp->TriggerFunc();	break;
						case SDLK_1:
							if (InRange(CurrentMainView,CurrentMainView_BVT_1,CurrentMainView_BVT_10))
								SetCurrentMainView(CurrentMainView_BVT_1);
							break;
						case SDLK_2:
							if (InRange(CurrentMainView,CurrentMainView_BVT_1,CurrentMainView_BVT_10))
								SetCurrentMainView(CurrentMainView_BVT_5);
							break;
						case SDLK_3:
							if (InRange(CurrentMainView,CurrentMainView_BVT_1,CurrentMainView_BVT_10))
								SetCurrentMainView(CurrentMainView_BVT_10);
							break;
						case SDLK_4:
							if (CurrentMainView==CurrentMainView_LVT_Normal)//??
								SetCurrentMainView(CurrentMainView_BVT_0);
							break;
						case SDLK_5:
							if (CurrentMainView==CurrentMainView_BVT_0)
								SetCurrentMainView(CurrentMainView_LVT_Normal);
							break;
					}
				}
				else if (keyevent->mod&KMOD_SHIFT)
				{
					switch (keyevent->keyCode)
					{
						case SDLK_DELETE:	Operation_Delete(0);		break;
					}
				}
				else switch (keyevent->keyCode)
				{
					case SDLK_F2:	Operation_Rename(0);	break;
					case SDLK_F5:	Operation_Refresh(0);	break;
					case SDLK_F11:	MainWindow->IsFullScreen()?MainWindow->CancelFullScreen():MainWindow->SetWindowFullScreen();	break;
					case SDLK_AC_BACK:
					case SDLK_BACKSPACE:	Bu_GoBack->TriggerFunc();	break;
					case SDLK_SPACE:
						//<<preview...
						break;
					case SDLK_DELETE:	Operation_Recycle(0);	break;
					case SDLK_RETURN:
					{
						int pos=MainView_GetCurrentSelectItem();
						if (pos!=-1)
							MainViewFunc(MainView_GetFuncData(pos),pos,2);
						break;
					}
					case SDLK_UP:
					case SDLK_DOWN:
					case SDLK_LEFT:
					case SDLK_RIGHT:
						if (MainView_GetItemCnt()!=0)
							if (int p=MainView_GetCurrentSelectItem();p!=-1)
							{
								if (InRange(CurrentMainView,CurrentMainView_BVT_0,CurrentMainView_BVT_10)||InThisSet(CurrentMainView,CurrentMainView_BVT_RecycleBin))
									BVT_MainView->SetSelectBlock(p=EnsureInRange(p+(keyevent->keyCode==SDLK_UP?-BVT_MainView->GetEachLineBlocks()
																				:(keyevent->keyCode==SDLK_DOWN?BVT_MainView->GetEachLineBlocks()
																				:(keyevent->keyCode==SDLK_LEFT?-1:1))),0,BVT_MainView->GetBlockCnt()-1));
								else LVT_MainView->SetSelectLine(p=EnsureInRange(p+(keyevent->keyCode==SDLK_UP||keyevent->keyCode==SDLK_LEFT?-1:1),0,LVT_MainView->GetListCnt()-1));
								UpdateMainViewSelection(p,1,keyevent->timeStamp);
							}
							else MainView_SetSelectItem(0),UpdateMainViewSelection(0,1,keyevent->timeStamp);
						break;
					default:
					{
						char quickSelect=0;
						if (InRange(keyevent->keyCode,SDLK_a,SDLK_z))
							quickSelect=keyevent->keyCode-SDLK_a+'a';
						else if (InRange(keyevent->keyCode,SDLK_0,SDLK_9))
							quickSelect=keyevent->keyCode-SDLK_0+'0';
						int itemcnt=MainView_GetItemCnt();
						if (quickSelect!=0&&itemcnt>0)
						{
							static int p=-1;
							static PFE_Path curpath;
							if (curpath!=CurrentPath)
								curpath=CurrentPath,p=-1;
							for (int i=itemcnt;i>=0;--i)
								if (i>=1)
								{
									p=(p+1)%itemcnt;
									const string &name=MainView_GetFuncData(p).path.name;
									if (!name.empty()&&Atoa(name[0])==quickSelect)
									{
										MainView_SetSelectItem(p);
										break;
									}
								}
								else SetSystemBeep(SetSystemBeep_Warning);
						}
						break;
					}
				}
			else if (keyevent->keyType==PUI_KeyEvent::Key_Up&&keyevent->win->GetKeyboardInputWg()==NULL)
				if (keyevent->mod&KMOD_SHIFT)
				{
					switch (keyevent->keyCode)
					{
						case SDLK_f:		PopupFunc_SearchInThisDir();	break;
					}
				}
			break;
		}
	}
	static bool ForceQuitIfFileOperationIsNotFree=0;
	if (quitflag==1&&!ForceQuitIfFileOperationIsNotFree&&!IsFileOperationFree())
	{
		quitflag=0;
		SetSystemBeep(SetSystemBeep_Warning);
		auto msgbx=new MessageBoxButton<bool*>(0,PUIT("警告"),PUIT("文件操作任务尚未完成，是否中止操作?"));
		msgbx->SetBackgroundColor(DUC_MessageLayerBackground);
		msgbx->SetTopAreaColor(DUC_MessageLayerTopAreaColor);
		msgbx->AddButton(PUIT("继续"),NULL,&ForceQuitIfFileOperationIsNotFree);
		msgbx->AddButton(PUIT("中止"),
			[](bool *&forcequit)
			{
				*forcequit=1;
				PUI_SendEvent(new PUI_Event(PUI_Event::Event_Quit));
			},&ForceQuitIfFileOperationIsNotFree);
	}
	return 0;
}

int SDL_main(int argc,char **argv)
{
	PAL_SingleProcessController SingleProcess("PAL_FileExplorer_ProcessMutex",
		new TypeFuncAndData <int> ([](int &funcdata,int usercode)->int
		{
			if (usercode==PAL_SingleProcessController::EventCode_SomeOneRun)
				PUI_SendFunctionEvent([](void*){MainWindow->SetForeground();});
			return 0;
		},0));
	if (SingleProcess.IsExsit())
		return cerr<<"Error: PAL_FileExplorer: Instance exsit, raise it's window and exit!"<<endl,-1;
	CheckErrorAndDDReturn(PAL_FileExplorer_Init(argc,argv),1);
	PUI_EasyEventLoop(EventLoopCallBack);
	CheckErrorAndDDReturn(PAL_FileExplorer_Quit(),2);
	return 0;
}
