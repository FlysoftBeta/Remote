#ifndef RE_NATIVE_RUNTIME_H_
#define RE_NATIVE_RUNTIME_H_
namespace re::native {
// Process lifetime runtime shared by RTC and Mojo. Never call from DllMain.
void EnsureRuntime();
}  // namespace re::native
#endif
