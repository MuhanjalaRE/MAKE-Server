#pragma once

// Name: mace, Version: 1.9.1.12285


/*!!DEFINE!!*/

/*!!HELPER_DEF!!*/

/*!!HELPER_INC!!*/

#ifdef _MSC_VER
	#pragma pack(push, 0x01)
#endif

namespace CG
{
//---------------------------------------------------------------------------
// Classes
//---------------------------------------------------------------------------

// BlueprintGeneratedClass WSkin_GrenadeLauncher_Default.WSkin_GrenadeLauncher_Default_C
// 0x0008 (FullSize[0x0300] - InheritedSize[0x02F8])
class AWSkin_GrenadeLauncher_Default_C : public AWSkin_DefaultSetup_C
{
public:
	struct FPointerToUberGraphFrame                    UberGraphFrame;                                            // 0x02F8(0x0008) (ZeroConstructor, Transient, DuplicateTransient)


	static UClass* StaticClass()
	{
		static UClass* ptr = nullptr;
		if(!ptr){
			ptr = UObject::FindClass("BlueprintGeneratedClass WSkin_GrenadeLauncher_Default.WSkin_GrenadeLauncher_Default_C");
		}
		return ptr;
	}



	void ReceiveBeginPlay();
	void ExecuteUbergraph_WSkin_GrenadeLauncher_Default(int EntryPoint);
};

}

#ifdef _MSC_VER
	#pragma pack(pop)
#endif
