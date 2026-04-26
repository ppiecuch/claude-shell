# Substitutes @APP_VERSION@, @APP_BUILD_NUMBER@, @BUILD_DATE@,
# @REPO_URL@, @ISSUES_URL@ in TEMPLATE_IN and writes TEMPLATE_OUT.
# Run via: cmake -DAPP_VERSION=… -DAPP_BUILD_NUMBER=… -DTEMPLATE_IN=… -DTEMPLATE_OUT=… -P configure-help.cmake

if(NOT DEFINED APP_VERSION OR NOT DEFINED APP_BUILD_NUMBER OR
   NOT DEFINED TEMPLATE_IN OR NOT DEFINED TEMPLATE_OUT)
    message(FATAL_ERROR "Required: APP_VERSION, APP_BUILD_NUMBER, TEMPLATE_IN, TEMPLATE_OUT")
endif()

if(NOT DEFINED REPO_URL)
    set(REPO_URL "https://github.com/ppiecuch/claude-shell")
endif()
if(NOT DEFINED ISSUES_URL)
    set(ISSUES_URL "https://github.com/ppiecuch/claude-shell/issues")
endif()

string(TIMESTAMP BUILD_DATE "%Y-%m-%d" UTC)

configure_file("${TEMPLATE_IN}" "${TEMPLATE_OUT}" @ONLY)
