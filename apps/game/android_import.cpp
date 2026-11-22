// Phones and tablets (task 4.20, owner's option B): the player's disc image is imported in
// the game. Android does not let an app read a .cue and its .bin files from the phone's
// shared folders, so the player picks them in the system's file picker (several at once)
// and the game copies them into its own folder, where it reads them from then on.
//
// SDL opens the picked files (content:// addresses) for reading; their names, which the
// .cue refers to, come from Android's ContentResolver through JNI.

#if defined(__ANDROID__) && defined(VIB_ANDROID_FLAT)

#include <SDL3/SDL.h>
#include <jni.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "vib/bytes.h"
#include "vib/disc_image.h"

namespace {

// The name the provider gives a picked file (OpenableColumns.DISPLAY_NAME), or "".
std::string display_name(const std::string& uri) {
  auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
  if (!env || !activity) return {};
  std::string name;
  jclass uri_class = env->FindClass("android/net/Uri");
  jmethodID parse = uri_class ? env->GetStaticMethodID(uri_class, "parse", "(Ljava/lang/String;)Landroid/net/Uri;") : nullptr;
  jstring text = env->NewStringUTF(uri.c_str());
  jobject u = parse ? env->CallStaticObjectMethod(uri_class, parse, text) : nullptr;
  jclass context = env->GetObjectClass(activity);
  jmethodID get_resolver = env->GetMethodID(context, "getContentResolver", "()Landroid/content/ContentResolver;");
  jobject resolver = get_resolver ? env->CallObjectMethod(activity, get_resolver) : nullptr;
  if (u && resolver && !env->ExceptionCheck()) {
    jclass resolver_class = env->GetObjectClass(resolver);
    jmethodID query = env->GetMethodID(resolver_class, "query",
                                       "(Landroid/net/Uri;[Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;)Landroid/database/Cursor;");
    jclass string_class = env->FindClass("java/lang/String");
    jobjectArray columns = env->NewObjectArray(1, string_class, nullptr);
    jstring column = env->NewStringUTF("_display_name");
    env->SetObjectArrayElement(columns, 0, column);
    jobject cursor = query ? env->CallObjectMethod(resolver, query, u, columns, nullptr, nullptr, nullptr) : nullptr;
    if (cursor && !env->ExceptionCheck()) {
      jclass cursor_class = env->GetObjectClass(cursor);
      jmethodID first = env->GetMethodID(cursor_class, "moveToFirst", "()Z");
      jmethodID get_string = env->GetMethodID(cursor_class, "getString", "(I)Ljava/lang/String;");
      jmethodID close = env->GetMethodID(cursor_class, "close", "()V");
      if (env->CallBooleanMethod(cursor, first) && !env->ExceptionCheck()) {
        auto value = static_cast<jstring>(env->CallObjectMethod(cursor, get_string, 0));
        if (value && !env->ExceptionCheck()) {
          const char* chars = env->GetStringUTFChars(value, nullptr);
          if (chars) name = chars;
          env->ReleaseStringUTFChars(value, chars);
          env->DeleteLocalRef(value);
        }
      }
      env->CallVoidMethod(cursor, close);
      env->DeleteLocalRef(cursor_class);
      env->DeleteLocalRef(cursor);
    }
    env->DeleteLocalRef(column);
    env->DeleteLocalRef(columns);
    env->DeleteLocalRef(string_class);
    env->DeleteLocalRef(resolver_class);
  }
  if (env->ExceptionCheck()) env->ExceptionClear();  // (a provider that will not say: no name)
  if (resolver) env->DeleteLocalRef(resolver);
  env->DeleteLocalRef(context);
  if (u) env->DeleteLocalRef(u);
  env->DeleteLocalRef(text);
  if (uri_class) env->DeleteLocalRef(uri_class);
  env->DeleteLocalRef(activity);
  // A file name only: nothing that could climb out of the game's folder.
  const std::size_t slash = name.find_last_of("/\\");
  if (slash != std::string::npos) name = name.substr(slash + 1);
  if (name == "." || name == "..") name.clear();
  return name;
}

std::vector<std::string> pick_files() {
  struct Answer {
    std::atomic<bool> done{false};
    std::vector<std::string> files;
  } answer;
  SDL_ShowOpenFileDialog(
      [](void* data, const char* const* files, int) {
        auto* a = static_cast<Answer*>(data);
        for (int i = 0; files && files[i]; ++i) a->files.push_back(files[i]);
        a->done = true;
      },
      &answer, nullptr, nullptr, 0, nullptr, true);
  while (!answer.done) {
    SDL_PumpEvents();
    SDL_Delay(10);
  }
  return answer.files;
}

bool copy_into(const std::string& uri, const std::filesystem::path& to) {
  SDL_IOStream* in = SDL_IOFromFile(uri.c_str(), "rb");
  if (!in) return false;
  std::ofstream out(to, std::ios::binary);
  std::vector<char> buf(1 << 20);
  bool ok = bool(out);
  for (std::size_t n; ok && (n = SDL_ReadIO(in, buf.data(), buf.size())) > 0;) ok = bool(out.write(buf.data(), std::streamsize(n)));
  SDL_CloseIO(in);
  return ok && bool(out);
}

bool ends_with_cue(std::string name) {
  for (char& c : name) c = char(std::tolower(static_cast<unsigned char>(c)));
  return name.size() > 4 && name.compare(name.size() - 4, 4, ".cue") == 0;
}

void say(const std::string& text) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Vib-Ribbon", text.c_str(), nullptr); }

}  // namespace

std::string import_disc(const std::string& folder) {
  for (;;) {
    const std::vector<std::string> files = pick_files();
    if (files.empty()) return {};
    const std::filesystem::path dir = vib::path_from_utf8(folder);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);  // (the disc imported before goes)
    std::filesystem::create_directories(dir, ec);
    std::string cue, problem;
    int copied = 0;
    for (std::size_t i = 0; i < files.size(); ++i) {
      std::string name = display_name(files[i]);
      if (name.empty()) name = "file" + std::to_string(i + 1);
      std::printf("Importing %s\n", name.c_str());
      if (!copy_into(files[i], dir / vib::path_from_utf8(name))) {
        problem = "Could not copy " + name + ".";
        break;
      }
      ++copied;
      if (ends_with_cue(name)) cue = (dir / vib::path_from_utf8(name)).string();
    }
    if (problem.empty() && cue.empty()) problem = "None of the files picked was a .cue file. Pick the .cue and all its .bin files together.";
    if (problem.empty()) {
      try {
        vib::DiscImage::open(vib::path_from_utf8(cue));  // (all its .bin files there?)
        std::printf("Imported %d files: %s\n", copied, cue.c_str());
        return cue;
      } catch (const std::exception& e) {
        problem = std::string("The disc image could not be read: ") + e.what() + "\n\nPick the .cue and all its .bin files together.";
      }
    }
    say(problem + "\n\nTry again, or cancel the picker to stop.");
  }
}

#endif
