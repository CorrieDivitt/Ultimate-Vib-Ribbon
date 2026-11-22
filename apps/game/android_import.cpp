// Phones and tablets (task 4.20, owner's option B): the player's disc image is imported in
// the game. Android does not let an app read a .cue's .bin files beside it in the phone's
// shared folders, so the player picks the folder that holds them (the owner's choice; our
// VibActivity's folder picker) and the game copies the .cue and its .bin files into its
// own folder, where it reads them from then on. Picking the files themselves is offered
// too (SDL's file picker; then any .bin files the .cue names that were not picked are
// asked for by name).
//
// SDL opens the files (content:// addresses) for reading; the picked files' names, which
// the .cue refers to, come from Android's ContentResolver through JNI.

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

struct Entry {
  std::string place, name, uri;  // (place: "" the picked folder, else a folder inside it)
};

// VibActivity's folder picker: false if cancelled (or not there), else the folder's files.
bool pick_folder(std::vector<Entry>* out) {
  out->clear();
  auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
  if (!env || !activity) return false;
  jclass cls = env->GetObjectClass(activity);
  jmethodID pick = env->GetStaticMethodID(cls, "pickFolder", "()V");
  jmethodID done = env->GetStaticMethodID(cls, "folderDone", "()Z");
  jmethodID picked = env->GetStaticMethodID(cls, "folderPicked", "()Z");
  jmethodID files = env->GetStaticMethodID(cls, "folderFiles", "()[Ljava/lang/String;");
  bool ok = false;
  if (pick && done && picked && files && !env->ExceptionCheck()) {
    env->CallStaticVoidMethod(cls, pick);
    while (!env->ExceptionCheck() && !env->CallStaticBooleanMethod(cls, done)) {
      SDL_PumpEvents();
      SDL_Delay(10);
    }
    ok = !env->ExceptionCheck() && env->CallStaticBooleanMethod(cls, picked);
    auto list = ok ? static_cast<jobjectArray>(env->CallStaticObjectMethod(cls, files)) : nullptr;
    if (list && !env->ExceptionCheck()) {
      const jsize n = env->GetArrayLength(list);
      auto text = [&](jsize i) {
        std::string r;
        auto str = static_cast<jstring>(env->GetObjectArrayElement(list, i));
        if (str) {
          if (const char* c = env->GetStringUTFChars(str, nullptr)) {
            r = c;
            env->ReleaseStringUTFChars(str, c);
          }
          env->DeleteLocalRef(str);
        }
        return r;
      };
      for (jsize i = 0; i + 2 < n; i += 3) {
        Entry e{text(i), text(i + 1), text(i + 2)};
        const std::size_t slash = e.name.find_last_of("/\\");
        if (slash != std::string::npos) e.name = e.name.substr(slash + 1);
        if (!e.name.empty() && e.name != "." && e.name != "..") out->push_back(e);
      }
      env->DeleteLocalRef(list);
    }
  }
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    ok = false;
  }
  env->DeleteLocalRef(cls);
  env->DeleteLocalRef(activity);
  return ok;
}

// The disc in a picked folder: each .cue there (the picked folder's first), copied with
// the files it names from its own folder; the first that opens. "" if none.
std::string import_from_folder(const std::vector<Entry>& entries, const std::filesystem::path& dir, std::string* problem) {
  std::vector<const Entry*> cues;
  for (const Entry& e : entries)
    if (ends_with_cue(e.name)) cues.push_back(&e);
  std::stable_sort(cues.begin(), cues.end(), [](const Entry* a, const Entry* b) { return a->place.empty() && !b->place.empty(); });
  for (const Entry* cue : cues) {
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path cue_path = dir / vib::path_from_utf8(cue->name);
    std::printf("Importing %s\n", cue->name.c_str());
    if (!copy_into(cue->uri, cue_path)) {
      *problem = "Could not copy " + cue->name + ".";
      continue;
    }
    bool copied_all = true;
    for (const std::string& want : missing_tracks(cue_path)) {
      const Entry* found = nullptr;
      for (const Entry& e : entries)
        if (e.place == cue->place && lower(e.name) == lower(want)) found = &e;
      std::printf("Importing %s\n", want.c_str());
      if (!found || !copy_into(found->uri, dir / vib::path_from_utf8(want))) {
        *problem = (found ? "Could not copy " : "The folder has no ") + want + (found ? "." : " (named in " + cue->name + ").");
        copied_all = false;
        break;
      }
    }
    if (!copied_all) continue;
    try {
      vib::DiscImage::open(cue_path);
      return cue_path.string();
    } catch (const std::exception& e) {
      *problem = std::string("The disc image could not be read: ") + e.what();
    }
  }
  if (cues.empty()) *problem = "There is no .cue file in that folder.";
  return {};
}

// Asks how to pick: 1 the folder, 2 the files, 0 cancel.
int ask_how(const std::string& text) {
  const SDL_MessageBoxButtonData buttons[] = {
      {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel"},
      {0, 2, "Pick files"},
      {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Pick folder"},
  };
  SDL_MessageBoxData box{};
  box.flags = SDL_MESSAGEBOX_INFORMATION;
  box.title = "Vib-Ribbon";
  box.message = text.c_str();
  box.numbuttons = 3;
  box.buttons = buttons;
  int chosen = 0;
  if (!SDL_ShowMessageBox(&box, &chosen)) return 1;
  return chosen;
}

// The older way: the files themselves, and then any the .cue names that were missed.
std::string import_from_files(const std::filesystem::path& dir, std::string* problem, bool* cancelled) {
  std::vector<std::string> files = pick_files();
  if (files.empty()) {
    *cancelled = true;
    return {};
  }
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);  // (the disc imported before goes)
  std::filesystem::create_directories(dir, ec);
  std::string cue;
  *problem = copy_picked(files, dir, &cue);
  if (problem->empty() && cue.empty()) *problem = std::string("None of the files picked was a .cue file.\n\n") + kHold;
  while (problem->empty()) {
    const std::vector<std::string> missing = missing_tracks(vib::path_from_utf8(cue));
    if (missing.empty()) break;
    std::string list;
    for (const std::string& m : missing) list += "\n" + m;
    say("Now pick the rest of the disc's files:\n" + list + "\n\n" + kHold);
    files = pick_files();
    if (files.empty()) {
      *cancelled = true;
      return {};
    }
    std::string ignored;
    *problem = copy_picked(files, dir, &ignored);
  }
  if (!problem->empty()) return {};
  try {
    vib::DiscImage::open(vib::path_from_utf8(cue));
    return cue;
  } catch (const std::exception& e) {
    *problem = std::string("The disc image could not be read: ") + e.what();
  }
  return {};
}

}  // namespace

std::string import_disc(const std::string& folder) {
  const std::filesystem::path dir = vib::path_from_utf8(folder);
  std::string text =
      "Choose your Vib-Ribbon disc image (your own copy of the PAL disc, SCES-02873): pick the folder that holds its "
      ".cue and .bin files. They are copied into the game, once.\n\nAndroid will not let apps use the Download "
      "folder itself or the phone's top folder: put the disc files in a folder of their own (for example "
      "Download/Vib-Ribbon) and pick that.";
  for (;;) {
    const int how = ask_how(text);
    if (how == 0) return {};
    std::string problem, cue;
    if (how == 1) {
      std::vector<Entry> entries;
      if (pick_folder(&entries)) {
        cue = import_from_folder(entries, dir, &problem);
      } else {
        problem = "No folder was picked.";
      }
    } else {
      bool cancelled = false;
      cue = import_from_files(dir, &problem, &cancelled);
      if (cancelled && problem.empty()) problem = "No files were picked.";
    }
    if (!cue.empty()) {
      std::printf("Imported disc: %s\n", cue.c_str());
      return cue;
    }
    text = problem + "\n\nTry again: pick the folder that holds the disc's .cue and .bin files, or pick the files themselves.";
  }
}

#endif
