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

std::string lower(std::string text) {
  for (char& c : text) c = char(std::tolower(static_cast<unsigned char>(c)));
  return text;
}

// The track files a .cue names (its FILE lines) that are not in the folder yet. A file
// there whose name differs only in letter case is renamed to the .cue's spelling.
std::vector<std::string> missing_tracks(const std::filesystem::path& cue) {
  std::vector<std::string> missing;
  std::ifstream in(cue);
  const std::filesystem::path dir = cue.parent_path();
  for (std::string line; std::getline(in, line);) {
    std::size_t at = line.find_first_not_of(" \t");
    if (at == std::string::npos || lower(line.substr(at, 5)) != "file ") continue;
    std::string name;
    const std::size_t open = line.find('"', at);
    const std::size_t close = open == std::string::npos ? open : line.find('"', open + 1);
    if (close != std::string::npos) {
      name = line.substr(open + 1, close - open - 1);
    } else {  // (unquoted: the word after FILE)
      std::size_t b = line.find_first_not_of(" \t", at + 5);
      std::size_t e = b == std::string::npos ? b : line.find_first_of(" \t\r", b);
      if (b != std::string::npos) name = line.substr(b, e == std::string::npos ? std::string::npos : e - b);
    }
    const std::size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    if (name.empty()) continue;
    const std::filesystem::path want = dir / vib::path_from_utf8(name);
    std::error_code ec;
    if (std::filesystem::exists(want, ec)) continue;
    bool found = false;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
      if (lower(entry.path().filename().string()) == lower(name)) {
        std::filesystem::rename(entry.path(), want, ec);
        found = !ec;
        break;
      }
    }
    if (!found && std::find(missing.begin(), missing.end(), name) == missing.end()) missing.push_back(name);
  }
  return missing;
}

// Copies the picked files into the folder; the .cue among them (if any) in *cue.
std::string copy_picked(const std::vector<std::string>& files, const std::filesystem::path& dir, std::string* cue) {
  for (std::size_t i = 0; i < files.size(); ++i) {
    std::string name = display_name(files[i]);
    if (name.empty()) name = "file" + std::to_string(i + 1);
    std::printf("Importing %s\n", name.c_str());
    if (!copy_into(files[i], dir / vib::path_from_utf8(name))) return "Could not copy " + name + ".";
    if (ends_with_cue(name)) *cue = (dir / vib::path_from_utf8(name)).string();
  }
  return {};
}

const char* const kHold = "Tip: in the file picker, press and hold a file to pick several at once.";

}  // namespace

std::string import_disc(const std::string& folder) {
  const std::filesystem::path dir = vib::path_from_utf8(folder);
  for (;;) {
    std::vector<std::string> files = pick_files();
    if (files.empty()) return {};
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);  // (the disc imported before goes)
    std::filesystem::create_directories(dir, ec);
    std::string cue;
    std::string problem = copy_picked(files, dir, &cue);
    if (problem.empty() && cue.empty()) problem = std::string("None of the files picked was a .cue file. Pick the .cue and all its .bin files.\n\n") + kHold;
    // Files the .cue names that were not picked: ask for those (added to the ones copied).
    while (problem.empty()) {
      const std::vector<std::string> missing = missing_tracks(vib::path_from_utf8(cue));
      if (missing.empty()) break;
      std::string list;
      for (const std::string& m : missing) list += "\n" + m;
      say("Now pick the rest of the disc's files:\n" + list + "\n\n" + kHold);
      files = pick_files();
      if (files.empty()) return {};
      std::string ignored;
      problem = copy_picked(files, dir, &ignored);
    }
    if (problem.empty()) {
      try {
        vib::DiscImage::open(vib::path_from_utf8(cue));
        std::printf("Imported disc: %s\n", cue.c_str());
        return cue;
      } catch (const std::exception& e) {
        problem = std::string("The disc image could not be read: ") + e.what();
      }
    }
    say(problem + "\n\nTry again, or cancel the picker to stop.");
  }
}

#endif
