#pragma once

#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/Basic/Builtins.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/raw_ostream.h"
#include <functional>
#include <vector>
#include <string>
#include <map>
#include <utility>
#include <regex>
#include <eosio/utils.hpp>

namespace eosio { namespace cdt {

struct generation_utils {
   std::function<void()> error_handler;
   std::vector<std::string> resource_dirs;
   std::string contract_name;

   generation_utils( std::function<void()> err ) : error_handler(err), resource_dirs({"./"}) {}
   generation_utils( std::function<void()> err, const std::vector<std::string>& paths ) : error_handler(err), resource_dirs(paths) {}

   static inline clang::QualType get_ignored_type( const clang::QualType& type) {
      auto get = [&](const clang::Type* pt) {
         if (auto tst = llvm::dyn_cast<clang::TemplateSpecializationType>(pt))
            if (auto decl = llvm::dyn_cast<clang::RecordType>(tst->desugar()))
               if (decl->getDecl()->isEosioIgnore()) 
                  return tst->getArg(0).getAsType();
         return type;
      };

      const clang::Type* t;
      if ( auto pt = llvm::dyn_cast<clang::ElaboratedType>(type.getTypePtr()) ) {
         t = pt->desugar().getTypePtr();
      }
      else
         t = type.getTypePtr();
      return get(t);
   }


   inline void set_contract_name( const std::string& cn ) { contract_name = cn; }
   inline std::string get_contract_name()const { return contract_name; }
   inline void set_resource_dirs( const std::vector<std::string>& rd ) {
      llvm::SmallString<128> cwd;
      auto has_real_path = llvm::sys::fs::real_path("./", cwd, true);
      if (!has_real_path)
         resource_dirs.push_back(cwd.str());
      for ( auto res : rd ) {
         llvm::SmallString<128> rp;
         auto has_real_path = llvm::sys::fs::real_path(res, rp, true);
         if (!has_real_path)
            resource_dirs.push_back(rp.str());
      }
   }

   static inline std::string get_action_name( const clang::CXXMethodDecl* decl ) {
      std::string action_name = "";
      auto tmp = decl->getEosioActionAttr()->getName();
      if (!tmp.empty())
         return tmp;
      return decl->getNameAsString();
   }
   static inline std::string get_notify_pair( const clang::CXXMethodDecl* decl ) {
      std::string notify_pair = "";
      auto tmp = decl->getEosioNotifyAttr()->getName();
      return tmp;
   }
   static inline std::string get_action_name( const clang::CXXRecordDecl* decl ) {
      std::string action_name = "";
      auto tmp = decl->getEosioActionAttr()->getName();
      if (!tmp.empty())
         return tmp;
      return decl->getName();
   }

   inline std::string read_file( const std::string& fname ) {
      for ( auto res : resource_dirs ) {
         if ( llvm::sys::fs::exists( res + "/" + fname ) ) {
            int fd;
            llvm::sys::fs::file_status stat;
            llvm::sys::fs::openFileForRead(res+"/"+fname, fd);
            llvm::sys::fs::status(fd, stat);
            llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> mb =
               llvm::MemoryBuffer::getOpenFile(fd, fname, stat.getSize());
            if (mb)
               return mb.get()->getBuffer().str();
         }
      }
      return {};
   }

   static inline std::string find_contract(const clang::Decl* decl, const std::string& cn) {
      std::string name = "";
      for (auto* attr: decl->getAttrs()) {
         if (auto* ec = llvm::dyn_cast<clang::EosioContractAttr>(attr)) {
           auto str = ec->getName().str();
           if (str.empty()) continue;
           name = str;
           if (name == cn) break;
         }
      }
      return name;
   }

   static inline bool is_eosio_contract( const clang::CXXMethodDecl* decl, const std::string& cn ) {
      std::string name = "";
      if (decl->isEosioContract())
         name = find_contract(decl, cn);
      else if (decl->getParent()->isEosioContract())
         name = find_contract(decl->getParent(), cn);
      if (name.empty()) {
         name = decl->getParent()->getName().str();
      }
      return name == cn;
   }

   static inline bool is_eosio_contract( const clang::CXXRecordDecl* decl, const std::string& cn ) {
      std::string name = "";
      auto pd = llvm::dyn_cast<clang::CXXRecordDecl>(decl->getParent());
      if (decl->isEosioContract()) {
         auto nm = find_contract(decl, cn);
         name = nm.empty() ? decl->getName().str() : nm;
      }
      else if (pd && pd->isEosioContract()) {
         auto nm = find_contract(pd, cn);
         name = nm.empty() ? pd->getName().str() : nm;
      }
      return cn == name;
   }

   static inline bool is_eosio_contract( const clang::TypedefNameDecl* decl, const std::string& cn ) {
      std::string name = "";
      auto dc = decl->getDeclContext();
      if (decl->hasEosioContracts()) {
         name = find_contract(decl, cn);
      }
      else if (dc) if (auto pd = llvm::dyn_cast<clang::CXXRecordDecl>(dc)) if (pd->isEosioContract()) {
         auto nm = find_contract(pd, cn);
         name = nm.empty() ? pd->getName().str() : nm;
      }
      return cn == name;
   }


   inline bool is_template_specialization( const clang::QualType& type, const std::vector<std::string>& names ) {
      auto check = [&](const clang::Type* pt) {
         if (auto tst = llvm::dyn_cast<clang::TemplateSpecializationType>(pt)) {
            if (auto rt = llvm::dyn_cast<clang::RecordType>(tst->desugar())) {
               if ( names.empty() ) {
                  return true;
               } else {
                  for ( auto name : names )
                     if ( rt->getDecl()->getName().str() == name ) {
                        return true;
                     }
               }
            }
         }
        return false;
      };
      bool is_specialization = false;
      if (auto pt = llvm::dyn_cast<clang::ElaboratedType>(type.getTypePtr())) {
         is_specialization = check(pt->desugar().getTypePtr());
      }
      else
         is_specialization = check(type.getTypePtr());

      return is_specialization;
   }
   inline clang::TemplateArgument get_template_argument( const clang::QualType& type, int index = 0 ) {
      auto ret = [&](const clang::Type* t) {
         auto tst = llvm::dyn_cast<clang::TemplateSpecializationType>(t);
         if (tst) {
            auto arg = tst->getArg(index);
            return arg.getAsType();
         }
         std::cout << "Internal error, wrong type of template specialization\n";
         error_handler();
         return tst->getArg(index).getAsType();
      };
      if (auto pt = llvm::dyn_cast<clang::ElaboratedType>(type.getTypePtr()))
         return ret(pt->desugar().getTypePtr());
      return ret(type.getTypePtr());
   }

   std::string get_base_type_name( const clang::QualType& type ) {
      clang::QualType newType = type;
      std::string type_str = newType.getNonReferenceType().getAsString();
      return get_base_type_name(type_str);
   }

   std::string get_base_type_name( const std::string& type_str ) {
      int i = type_str.length()-1;
      int template_nested = 0;
      for (; i > 0; i--) {
         if (type_str[i] == '>') ++template_nested;
         if (type_str[i] == '<') --template_nested;
         if (type_str[i] == ':' && template_nested == 0) {
            return type_str.substr(i+1);
         }
         if (type_str[i] == ' ' && template_nested == 0) {
            static const std::string valid_prefixs[] = {"long", "signed", "unsigned" };
            bool valid = false;
            for (auto &s : valid_prefixs) {
               if (i >= s.length() && type_str.substr(i - s.length(), s.length()) == s &&
                   (i - s.length() == 0 || type_str[i - s.length() - 1] == ' ')) {
                  valid = true;
                  i -= s.length();
                  break;
               }
            }
            if (valid) continue;
            return type_str.substr(i+1);
         }
      }
      return type_str;
   }

   std::string _translate_type( const clang::QualType& type ) {
      clang::QualType newType = type;
      std::string type_str = newType.getNonReferenceType().getAsString();
      return _translate_type(get_base_type_name(type_str));
   }

   std::string _translate_type( const std::string& t ) {
      static std::map<std::string, std::string> translation_table =
      {
         {"unsigned __int128", "uint128"},
         {"__int128", "int128"},
         {"uint128_t", "uint128"},
         {"int128_t", "int128"},

         {"unsigned long long", "uint64"},
         {"long long", "int64"},
         {"uint64_t", "uint64"},
         {"int64_t", "int64"},

         {"unsigned long", "uint32"},
         {"long", "int32"},
         {"unsigned int", "uint32"},
         {"int", "int32"},
         {"uint32_t", "uint32"},
         {"int32_t", "int32"},

         {"unsigned short", "uint16"},
         {"short", "int16"},
         {"uint16_t", "uint16"},
         {"int16_t", "int16"},

         {"unsigned char", "uint8"},
         {"signed char", "int8"},
         {"char", "int8"},
         {"uint8_t", "uint8"},
         {"int8_t", "int8"},
         {"_Bool", "bool"},

         {"float",  "float32"},
         {"double", "float64"},
         {"long double", "float128"},

         {"unsigned_int", "varuint32"},
         {"signed_int",   "varint32"},

         {"block_timestamp", "block_timestamp_type"},
         {"capi_name",    "name"},
         {"capi_public_key", "public_key"},
         {"capi_signature", "signature"},
         {"capi_checksum160", "checksum160"},
         {"capi_checksum256", "checksum256"},
         {"capi_checksum512", "checksum512"},
         {"fixed_bytes_20", "checksum160"},
         {"fixed_bytes_32", "checksum256"},
         {"fixed_bytes_64", "checksum512"}
      };
      
      auto ret = translation_table[t];

      if (ret == "")
         return t;
      return ret;
   }

   inline std::string replace_in_name( std::string name ) {
      std::string ret = name;
      std::replace(ret.begin(), ret.end(), '<', '_');
      std::replace(ret.begin(), ret.end(), '>', '_');
      std::replace(ret.begin(), ret.end(), ',', '_');
      std::replace(ret.begin(), ret.end(), ' ', '_');
      std::replace(ret.begin(), ret.end(), ':', '_');
      return ret;
   }

   inline std::string get_template_name( const clang::QualType& type ) {
      auto pt = llvm::dyn_cast<clang::ElaboratedType>(type.getTypePtr());
      auto tst = llvm::dyn_cast<clang::TemplateSpecializationType>(pt ? pt->desugar().getTypePtr() : type.getTypePtr());
      std::string ret = tst->getTemplateName().getAsTemplateDecl()->getName().str()+"_";
      for (int i=0; i < tst->getNumArgs(); ++i) {
         auto arg = get_template_argument(type,i);
         if (arg.getAsExpr()) {
            auto ce = llvm::dyn_cast<clang::CastExpr>(arg.getAsExpr());
            if (ce) { 
               auto il = llvm::dyn_cast<clang::IntegerLiteral>(ce->getSubExpr());
               ret += std::to_string(il->getValue().getLimitedValue());
               if ( i < tst->getNumArgs()-1 )
                  ret += "_";
            }
         }
         else {
            ret += _translate_type(get_template_argument( type, i ).getAsType());
            if ( i < tst->getNumArgs()-1 )
               ret += "_";
         }
      }
      return _translate_type(replace_in_name(ret));
   }

   inline std::string translate_type( const clang::QualType& type0, bool ignore_type = true ) {
      clang::QualType type = type0;
      if (ignore_type)
         type = get_ignored_type(type);
      if ( is_template_specialization( type, {"ignore"} ) )
         return translate_type(get_template_argument( type ).getAsType() );
      else if ( is_template_specialization( type, {"binary_extension"} ) ) {
         auto t = translate_type(get_template_argument( type ).getAsType());
         return t+"$";
      }
      else if ( is_template_specialization( type, {"vector", "set", "deque", "list"} ) ) {
         auto t =translate_type(get_template_argument( type ).getAsType());
         return t=="int8" ? "bytes" : t+"[]";
      }
      else if ( is_template_specialization( type, {"optional"} ) )
         return translate_type(get_template_argument( type ).getAsType())+"?";
      else if ( is_template_specialization( type, {"map"} )) {
         auto t0 = translate_type(get_template_argument( type ).getAsType());
         auto t1 = translate_type(get_template_argument( type, 1).getAsType());
         return replace_in_name("pair_" + t0 + "_" + t1 + "[]");
      }
      else if ( is_template_specialization( type, {"pair"} )) {
         auto t0 = translate_type(get_template_argument( type ).getAsType());
         auto t1 = translate_type(get_template_argument( type, 1).getAsType());
         return replace_in_name("pair_" + t0 + "_" + t1);
      }
      else if ( is_template_specialization( type, {"tuple"} )) {
         auto pt = llvm::dyn_cast<clang::ElaboratedType>(type.getTypePtr());
         auto tst = llvm::dyn_cast<clang::TemplateSpecializationType>( pt ? pt->desugar().getTypePtr() : type.getTypePtr() );
         std::string ret = "tuple_";
         for (int i=0; i < tst->getNumArgs(); ++i) {
            ret += _translate_type(get_template_argument( type, i ).getAsType());
            if ( i < tst->getNumArgs()-1 )
               ret += "_";
         }
         return replace_in_name(ret);
      }
      else if ( is_template_specialization( type, {} )) {
         auto pt = llvm::dyn_cast<clang::ElaboratedType>(type.getTypePtr());
         auto tst = llvm::dyn_cast<clang::TemplateSpecializationType>(pt ? pt->desugar().getTypePtr() : type.getTypePtr() );
         std::string ret = tst->getTemplateName().getAsTemplateDecl()->getName().str()+"_";
         for (int i=0; i < tst->getNumArgs(); ++i) {
            auto arg = get_template_argument(type,i);
            if (auto ce = arg.getKind() == clang::TemplateArgument::ArgKind::Expression
                  ? llvm::dyn_cast<clang::CastExpr>(arg.getAsExpr()) : nullptr) {
               auto il = llvm::dyn_cast<clang::IntegerLiteral>(ce->getSubExpr());
               ret += std::to_string(il->getValue().getLimitedValue());
               if ( i < tst->getNumArgs()-1 )
                  ret += "_";
            }
            else {
               ret += translate_type(get_template_argument( type, i ).getAsType());
               if ( i < tst->getNumArgs()-1 )
                  ret += "_";
            }
         }
         return _translate_type(replace_in_name(ret));
      }
      return _translate_type( type );
   }

   inline bool is_builtin_type( const std::string& t ) {
      static const std::set<std::string> builtins =
      {
         "bool",
         "int8",
         "uint8",
         "int16",
         "uint16",
         "int32",
         "uint32",
         "int64",
         "uint64",
         "int128",
         "uint128",
         "varint32",
         "varuint32",
         "float32",
         "float64",
         "float128",
         "time_point",
         "time_point_sec",
         "name",
         "bytes",
         "string",
         "block_timestamp_type",
         "name",
         "checksum160",
         "checksum256",
         "checksum512",
         "public_key",
         "signature",
         "public_key",
         "signature",
         "symbol",
         "symbol_code",
         "asset"
      };
      return builtins.count(_translate_type(t)) >= 1;
   }

   inline bool is_builtin_type( const clang::QualType& t ) {
      std::string nt = translate_type(t);
      return is_builtin_type(nt);
   }

   inline bool is_cxx_record( const clang::QualType& t ) {
      return t.getTypePtr()->isRecordType();
   }

   inline std::string get_type( const clang::QualType& t ) {
      return translate_type(get_ignored_type(t), false);
   }

   inline std::string get_type_alias_string( const clang::QualType& t ) {
      if (auto dt = llvm::dyn_cast<clang::TypedefType>(t.getTypePtr()))
         return get_type(dt->desugar());
      else if (auto dt = llvm::dyn_cast<clang::ElaboratedType>(t.getTypePtr()))
         return get_type_alias_string(dt->desugar());
      return get_type(t);
   }

   inline std::vector<clang::QualType> get_type_alias( const clang::QualType& t ) {
      if (auto dt = llvm::dyn_cast<clang::TypedefType>(t.getTypePtr()))
         return {dt->desugar()};
      else if (auto dt = llvm::dyn_cast<clang::ElaboratedType>(t.getTypePtr()))
         return get_type_alias(dt->desugar());
      return {};
   }

   inline bool is_aliasing( const clang::QualType& t ) {
      if (is_builtin_type(t))
         return false;
      if (get_base_type_name(t).find("<") != std::string::npos) return false;
      return get_base_type_name(t).compare(get_type_alias_string(t)) != 0;
   }
};
}} // ns eosio::cdt
