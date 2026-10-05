add_executable(forumer_core_tests
    test_main.cpp
    test_crypto.cpp
    test_vault.cpp
    test_mnemonic.cpp
    test_identity.cpp
    test_account_store.cpp
    test_post.cpp
    test_post_store.cpp
    test_sync.cpp
    test_thread.cpp
    test_flood.cpp
    test_inbox.cpp
    test_history.cpp
)
 
target_link_libraries(forumer_core_tests PRIVATE forumer_core)
 
add_test(NAME forumer_core_tests COMMAND forumer_core_test