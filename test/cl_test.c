/**
 * @file cl_test.c
 * @author Rastislav Szabo <raszabo@cisco.com>, Lukas Macko <lmacko@cisco.com>
 * @brief Client Library unit tests.
 *
 * @copyright
 * Copyright 2016 Cisco Systems, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>
#include <cmocka.h>

#include "sysrepo.h"
#include "sr_logger.h"

static int
logging_setup(void **state)
{
    sr_logger_set_level(SR_LL_DBG, SR_LL_ERR); /* print debugs to stderr */
    return 0;
}

static int
sysrepo_setup(void **state)
{
    sr_conn_ctx_t *conn = NULL;
    int rc = SR_ERR_OK;

    logging_setup(state);

    /* connect to sysrepo */
    rc = sr_connect("cl_test", true, &conn);
    assert_int_equal(rc, SR_ERR_OK);

    *state = (void*)conn;
    return 0;
}

static int
sysrepo_teardown(void **state)
{
    sr_conn_ctx_t *conn = *state;
    assert_non_null(conn);

    /* disconnect from sysrepo */
    sr_disconnect(conn);

    return 0;
}

static void
cl_connection_test(void **state) {
    sr_conn_ctx_t *conn1 = NULL, *conn2 = NULL;
    sr_session_ctx_t *sess1 = NULL, *sess2 = NULL, *sess_other = NULL;
    int rc = 0;

    /* connect to sysrepo - conn 1 */
    rc = sr_connect("cl_test", true, &conn1);
    assert_int_equal(rc, SR_ERR_OK);
    assert_non_null(conn1);

    /* connect to sysrepo - conn 2 */
    rc = sr_connect("cl_test", true, &conn2);
    assert_int_equal(rc, SR_ERR_OK);
    assert_non_null(conn2);

    /* start a new session in conn 1 */
    rc = sr_session_start(conn1, "alice", SR_DS_RUNNING, &sess1);
    assert_int_equal(rc, SR_ERR_OK);
    assert_non_null(sess1);

    /* start few new sessions in conn 2 */
    rc = sr_session_start(conn2, "bob1", SR_DS_CANDIDATE, &sess2);
    assert_int_equal(rc, SR_ERR_OK);
    assert_non_null(sess2);
    rc = sr_session_start(conn2, "bob2", SR_DS_STARTUP, &sess_other);
    assert_int_equal(rc, SR_ERR_OK);
    assert_non_null(sess2);
    rc = sr_session_start(conn2, "bob3", SR_DS_CANDIDATE, &sess2);
    assert_int_equal(rc, SR_ERR_OK);
    assert_non_null(sess2);
    rc = sr_session_start(conn2, "bob4", SR_DS_CANDIDATE, &sess2);
    assert_int_equal(rc, SR_ERR_OK);
    assert_non_null(sess2);

    /* stop the session 1 */
    rc = sr_session_stop(sess1);
    assert_int_equal(rc, SR_ERR_OK);

    /* not all sessions in conn2 stopped explicitly - should be released automatically by disconnect */
    rc = sr_session_stop(sess_other);
    assert_int_equal(rc, SR_ERR_OK);
    rc = sr_session_stop(sess2);
    assert_int_equal(rc, SR_ERR_OK);

    /* disconnect from sysrepo - conn 2 */
    sr_disconnect(conn2);

    /* disconnect from sysrepo - conn 1 */
    sr_disconnect(conn1);
}

static void
cl_get_item_test(void **state) {
    sr_conn_ctx_t *conn = *state;
    assert_non_null(conn);

    sr_session_ctx_t *session = NULL;
    sr_val_t *value = NULL;
    int rc = 0;

    /* start a session */
    rc = sr_session_start(conn, "alice", SR_DS_CANDIDATE, &session);
    assert_int_equal(rc, SR_ERR_OK);

    /* perform a get-item request */

    /* illegal xpath */
    rc = sr_get_item(session, "^&((", &value);
    assert_int_equal(SR_ERR_INVAL_ARG, rc);
    assert_null(value);

    /* unknown model */
    rc = sr_get_item(session, "/unknown-model:abc", &value);
    assert_int_equal(SR_ERR_UNKNOWN_MODEL, rc);
    assert_null(value);

    /* not existing data tree*/
    rc = sr_get_item(session, "/small-module:item", &value);
    assert_int_equal(SR_ERR_NOT_FOUND, rc);
    assert_null(value);

    /* existing leaf */
    rc = sr_get_item(session, "/example-module:container/list[key1='key1'][key2='key2']/leaf", &value);
    assert_int_equal(rc, SR_ERR_OK);
    assert_non_null(value);
    assert_int_equal(SR_STRING_T, value->type);
    assert_string_equal("Leaf value", value->data.string_val);
    assert_string_equal("/example-module:container/list[key1='key1'][key2='key2']/leaf", value->xpath);
    sr_free_val_t(value);

    /* container */
    rc = sr_get_item(session, "/example-module:container", &value);
    assert_int_equal(rc, SR_ERR_OK);
    assert_non_null(value);
    assert_int_equal(SR_CONTAINER_T, value->type);
    assert_string_equal("/example-module:container", value->xpath);
    sr_free_val_t(value);

    /* list */
    rc = sr_get_item(session, "/example-module:container/list[key1='key1'][key2='key2']", &value);
    assert_int_equal(rc, SR_ERR_OK);
    assert_non_null(value);
    assert_int_equal(SR_LIST_T, value->type);
    assert_string_equal("/example-module:container/list[key1='key1'][key2='key2']", value->xpath);
    sr_free_val_t(value);


    /* stop the session */
    rc = sr_session_stop(session);
    assert_int_equal(rc, SR_ERR_OK);
}

static void
cl_get_items_test(void **state) {
    sr_conn_ctx_t *conn = *state;
    assert_non_null(conn);

    sr_session_ctx_t *session = NULL;
    sr_val_t **values = NULL;
    size_t values_cnt = 0;
    int rc = 0;

    /* start a session */
    rc = sr_session_start(conn, "alice", SR_DS_CANDIDATE, &session);
    assert_int_equal(rc, SR_ERR_OK);
    assert_non_null(session);

    /* perform a get-items request */

    /* illegal xpath */
    rc = sr_get_items(session, "^&((",  &values, &values_cnt);
    assert_int_equal(SR_ERR_INVAL_ARG, rc);

    /* unknown model */
    rc = sr_get_items(session, "/unknown-model:abc",  &values, &values_cnt);
    assert_int_equal(SR_ERR_UNKNOWN_MODEL, rc);

    /* not existing data tree*/
    rc = sr_get_items(session, "/small-module:item",  &values, &values_cnt);
    assert_int_equal(SR_ERR_NOT_FOUND, rc);

    /* container */
    rc = sr_get_items(session, "/ietf-interfaces:interfaces", &values, &values_cnt);
    assert_int_equal(rc, SR_ERR_OK);
    assert_int_equal(3, values_cnt);
    sr_free_values_t(values, values_cnt);

    /* list without keys */
    rc = sr_get_items(session, "/ietf-interfaces:interfaces/interface", &values, &values_cnt);
    assert_int_equal(rc, SR_ERR_OK);
    assert_int_equal(3, values_cnt);
    sr_free_values_t(values, values_cnt);

    /* list with keys */
    rc = sr_get_items(session, "/ietf-interfaces:interfaces/interface[name='eth0']", &values, &values_cnt);
    assert_int_equal(rc, SR_ERR_OK);
    assert_int_equal(5, values_cnt);
    sr_free_values_t(values, values_cnt);

    //TODO leaf-list

    /* stop the session */
    rc = sr_session_stop(session);
    assert_int_equal(rc, SR_ERR_OK);
}

static void
cl_get_items_iter_test(void **state) {
    sr_conn_ctx_t *conn = *state;
    assert_non_null(conn);

    sr_session_ctx_t *session = NULL;
    sr_val_t *value = NULL;
    int rc = 0;

    /* start a session */
    rc = sr_session_start(conn, "alice", SR_DS_CANDIDATE, &session);
    assert_int_equal(rc, SR_ERR_OK);
    assert_non_null(session);

    /* perform a get-items_iter request */
    sr_val_iter_t *it;

    /* illegal xpath */
    rc = sr_get_items_iter(session, "^&((", true, &it);
    assert_int_equal(SR_ERR_INVAL_ARG, rc);
    assert_non_null(it);

    /* container */
    rc = sr_get_items_iter(session, "/example-module:container", true, &it);
    assert_int_equal(rc, SR_ERR_OK);
    assert_non_null(it);
    for (int i = 0; i < 6; i++) {
        rc = sr_get_item_next(session, it, &value);
        if (SR_ERR_NOT_FOUND == rc ){
            break;
        }
        assert_int_equal(SR_ERR_OK, rc);
        puts(value->xpath);
        sr_free_val_t(value);
    }
    sr_free_val_iter(it);


    /* stop the session */
    rc = sr_session_stop(session);
    assert_int_equal(rc, SR_ERR_OK);
}

int
main() {
    const struct CMUnitTest tests[] = {
            cmocka_unit_test_setup_teardown(cl_connection_test, logging_setup, NULL),
            cmocka_unit_test_setup_teardown(cl_get_item_test, sysrepo_setup, sysrepo_teardown),
            cmocka_unit_test_setup_teardown(cl_get_items_test, sysrepo_setup, sysrepo_teardown),
            cmocka_unit_test_setup_teardown(cl_get_items_iter_test, sysrepo_setup, sysrepo_teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
