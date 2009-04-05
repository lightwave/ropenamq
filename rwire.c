/*
// Copyright (c) 2009, Chris Wong <chris@chriswongstudio.com> All rights
// reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// * Neither the name of Chris Wong Studio nor the names of its contributors
//   may be used to endorse or promote products derived from this software
//   without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE. */

#include "ruby.h"
#include "wireapi.h"
#include <dlfcn.h>
#include <unistd.h>
#include <stdbool.h>

VALUE eAMQError;
VALUE eAMQDestroyedError;

VALUE cRWire;
VALUE cContent;
VALUE cConnection;
VALUE cSession;

#define DEF_STRING_SETTER(attr, amq_type) \
static VALUE rwire_##amq_type##_set_##attr(VALUE self, VALUE attr)\
{\
	amq_type##_t *p = NULL;\
\
	Data_Get_Struct(self, amq_type##_t, p);\
\
	char * str = StringValuePtr(attr);\
	if (str) {\
		if (amq_type##_set_##attr(p, str)) {\
			rb_raise(eAMQError, "Failed to set "#attr);\
		}\
	}\
	return self;\
}

#define DEF_STRING_GETTER(attr, amq_type) \
static VALUE rwire_##amq_type##_get_##attr(VALUE self)\
{\
	amq_type##_t * p = NULL;\
	\
	char * _value = NULL;\
	VALUE result;\
	\
	Data_Get_Struct(self, amq_type##_t, p);\
	if (p) {\
		_value = amq_type##_get_##attr(p);\
		if (!_value) {\
			rb_raise(eAMQError, "Failed to get "#attr);\
		}\
	}\
	\
	return rb_str_new2(_value);\
}

#define DEF_INT_GETTER(attr, amq_type, conversion_func) \
static VALUE rwire_##amq_type##_get_##attr(VALUE self)\
{\
	amq_type##_t * p = NULL;\
\
	Data_Get_Struct(self, amq_type##_t, p);\
\
	if (p) {\
		return conversion_func(amq_type##_get_##attr(p));\
	}\
\
	return conversion_func(0);\
}

#define DEF_INT_SETTER(attr, amq_type, conversion_func) \
static VALUE rwire_##amq_type##_set_##attr(VALUE self, VALUE v)\
{\
	amq_type##_t * p = NULL;\
\
	Data_Get_Struct(self, amq_type##_t, p);\
\
	if (p) {\
		amq_type##_set_##attr(p, conversion_func(v));\
	}\
\
	return conversion_func(0);\
}

#define DEF_CONTENT_BASIC_STRING_ATTR(attr) \
	DEF_STRING_GETTER(attr, amq_content_basic) \
	DEF_STRING_SETTER(attr, amq_content_basic)

#define DEF_CONTENT_BASIC_INT_GETTER(attr, conversion_func) \
	DEF_INT_GETTER(attr, amq_content_basic, conversion_func)

#define DEF_CONTENT_BASIC_INT_SETTER(attr, conversion_func) \
	DEF_INT_SETTER(attr, amq_content_basic, conversion_func)

#define DEF_CLIENT_CONNECTION_STRING_ATTR(attr) \
	DEF_STRING_GETTER(attr, amq_client_connection) \
	DEF_STRING_SETTER(attr, amq_client_connection)

#define DEF_CLIENT_CONNECTION_INT_GETTER(attr, conversion_func) \
	DEF_INT_GETTER(attr, amq_client_connection, conversion_func)

#define DEF_CLIENT_CONNECTION_INT_SETTER(attr, conversion_func) \
	DEF_INT_SETTER(attr, amq_client_connection, conversion_func)

#define CONNECTION_GET \
	amq_client_connection_t * c;\
	Data_Get_Struct(self, amq_client_connection_t, c)

#define TO_BOOL(v) (((v) != Qfalse) && !NIL_P(v))

// This malloc a buffer and copy the content of Ruby string into it.	Note
// that it's not a null terminated C string.
static char * new_blob_from_rb_str(VALUE rstr)
{
	char * src  = StringValuePtr(rstr);
	int    size = RSTRING_LEN(rstr);
	char * dest = malloc(size);
	memcpy(dest, src, size);

	return dest;
}

static VALUE rwire_init(VALUE self, VALUE trace_level)
{
	int opt_trace = FIX2INT(trace_level) || 0;

	if (opt_trace > 2)
	{
		amq_client_connection_animate(TRUE);
		amq_client_session_animate(TRUE);
	}

	return self;
}

static void rwire_connection_free(void * p)
{
	amq_client_connection_t * c = (amq_client_connection_t *)p;
	if (c) {
		fprintf(stderr, "AMQ connection not destroyed yet, calling destroy\n");
		amq_client_connection_destroy(&c);
	}
	else {
		fprintf(stderr, "AMQ connection's already destroyed\n");
	}
	free(c);
}

static VALUE rwire_connection_alloc(VALUE klass)
{
	amq_client_connection_t * c = NULL;
	return Data_Wrap_Struct(cConnection, 0, rwire_connection_free, c);
}

static VALUE rwire_connection_init(
	VALUE self,
	VALUE host,
	VALUE vhost,
	VALUE username,
	VALUE password,
	VALUE client_name,
	VALUE trace,
	VALUE timeout)
{
	icl_longstr_t *auth_data;
	char *_hostname    = StringValuePtr(host);
	char *_vhost       = StringValuePtr(vhost);
	char *_client_name = StringValuePtr(client_name);
	char *_username    = StringValuePtr(username);
	char *_password    = StringValuePtr(password);;

	//  Open all connections
	auth_data = amq_client_connection_auth_plain(_username, _password);
	amq_client_connection_t * c = amq_client_connection_new(
				_hostname,
				_vhost,
				auth_data,
				_client_name,
				FIX2INT(trace),
				FIX2INT(timeout));

	if (!c)
		rb_raise(eAMQError, "Failed to connect to AMQ broker");

	DATA_PTR(self) = c;

	return self;
}

/////////////////////////////////////////////////////////////////////////////
//
// Functions for RWire::Content
//
/////////////////////////////////////////////////////////////////////////////

// Helper function to destroy the underlying amq_content_basic_t object when
// GC free the corresponding Ruby Object
static void rwire_amq_content_basic_free(void *p)
{
	amq_content_basic_t * content = (amq_content_basic_t *)p;
	if (content) {
		amq_content_basic_unlink(&content);
	}
}

static VALUE rwire_amq_content_basic_alloc(VALUE klass)
{
	amq_content_basic_t * content = amq_content_basic_new();
	if (!content)
		rb_raise(rb_eRuntimeError, "Failed to create content object");
	VALUE rb_content = Data_Wrap_Struct(klass, 0, rwire_amq_content_basic_free, content);
	return rb_content;
}

static VALUE rwire_amq_content_basic_unlink(VALUE self)
{
	// NOTE: Use DATA_PTR to get the underlying ptr instead of the helper
	// macro Data_Get_Struct which makes a copy of the underlying struct.

	amq_content_basic_unlink((amq_content_basic_t**)&(DATA_PTR(self)));

	return self;
}

static VALUE rwire_amq_content_basic_set_body(VALUE self, VALUE value)
{
	amq_content_basic_t  *content = NULL;
	char *_value = StringValuePtr(value);
	Data_Get_Struct(self, amq_content_basic_t, content);

	int len = RSTRING(value)->len;
	char * copy = malloc(len);
	memcpy(copy, _value, len);

	if (amq_content_basic_set_body(content, copy, RSTRING(value)->len, free)) {
		rb_raise(eAMQError, "Failed to set content body");
	}
	return self;
}

static VALUE rwire_amq_content_basic_get_body(VALUE self)
{
	amq_content_basic_t * content = NULL;

	int64_t size = 0;
	char * _value = NULL;
	VALUE result;

	Data_Get_Struct(self, amq_content_basic_t, content);
	if (content) {
		size = amq_content_basic_get_body_size(content);
		_value = malloc(size+1);
		amq_content_basic_get_body(content, (byte *)_value, size);
		_value[size] = '\0';
		result = rb_str_new(_value, size);
	}
	else {
		result = rb_str_new2("");
	}

	return result;
}

static VALUE rwire_amq_client_session_new(VALUE self)
{
	amq_client_connection_t *connection = NULL;
	amq_client_session_t *session = NULL;
	VALUE rb_session = Qnil;

	Data_Get_Struct(self, amq_client_connection_t, connection);

	if (connection)
	{
		session = amq_client_session_new (connection);
		if (!session)
			rb_raise(eAMQError, "Failed to start a new session");

		rb_session = Data_Wrap_Struct(cSession, 0, 0, session);
	}
	else
		rb_raise(rb_eRuntimeError, "Server connection is dead");

	return (rb_session);
}

DEF_CONTENT_BASIC_STRING_ATTR(app_id)
DEF_CONTENT_BASIC_INT_GETTER (body_size, LL2NUM)
DEF_CONTENT_BASIC_INT_GETTER (class_id, INT2FIX)
DEF_CONTENT_BASIC_STRING_ATTR(content_encoding)
DEF_CONTENT_BASIC_STRING_ATTR(content_type)
DEF_CONTENT_BASIC_STRING_ATTR(correlation_id)
DEF_CONTENT_BASIC_INT_GETTER(delivery_mode, INT2NUM)
DEF_CONTENT_BASIC_INT_SETTER(delivery_mode, NUM2INT)
DEF_STRING_GETTER(exchange,    amq_content_basic)
DEF_CONTENT_BASIC_STRING_ATTR(expiration)
DEF_CONTENT_BASIC_STRING_ATTR(message_id)
DEF_CONTENT_BASIC_INT_GETTER(priority, INT2NUM)
DEF_CONTENT_BASIC_INT_SETTER(priority, NUM2INT)
DEF_CONTENT_BASIC_STRING_ATTR(reply_to)
DEF_STRING_GETTER(routing_key, amq_content_basic)
DEF_CONTENT_BASIC_INT_GETTER(timestamp, LL2NUM)
DEF_CONTENT_BASIC_INT_SETTER(timestamp, NUM2LL)
DEF_CONTENT_BASIC_STRING_ATTR(user_id)

/////////////////////////////////////////////////////////////////////////////
//
// Functions for RWire::Connection
//
/////////////////////////////////////////////////////////////////////////////
static VALUE rwire_connection_destroy(VALUE self)
{
	CONNECTION_GET;
	if (c) {
		amq_client_connection_destroy((amq_client_connection_t**)&(DATA_PTR(self)));
	}
	return self;
}

static VALUE rwire_amq_client_connection_get_alive(VALUE self)
{
	CONNECTION_GET;
	if (c) {
		return amq_client_connection_get_alive(c) ? Qtrue : Qfalse;
	}
	return Qfalse;
}

static VALUE rwire_amq_client_connection_get_silent(VALUE self)
{
	CONNECTION_GET;
	if (!c) {
		rb_raise(eAMQDestroyedError, "Connection has aleady been destroyed");
	}
	return amq_client_connection_get_silent(c) ? Qtrue : Qfalse;
}

static VALUE rwire_amq_client_connection_set_silent(VALUE self, VALUE silent)
{
	CONNECTION_GET;
	if (!c) {
		rb_raise(eAMQDestroyedError, "Connection has aleady been destroyed");
	}
	return amq_client_connection_set_silent(c, silent);
}

DEF_CLIENT_CONNECTION_INT_GETTER(channel_max, UINT2NUM)
DEF_CLIENT_CONNECTION_INT_GETTER(class_id, INT2FIX)
DEF_STRING_GETTER(error_text, amq_client_connection)
DEF_CLIENT_CONNECTION_INT_GETTER(frame_max, ULONG2NUM)
DEF_CLIENT_CONNECTION_INT_GETTER(heartbeat, INT2FIX)
DEF_STRING_GETTER(known_hosts, amq_client_connection)
DEF_CLIENT_CONNECTION_INT_GETTER(method_id, INT2FIX)
DEF_CLIENT_CONNECTION_INT_GETTER(reply_code, INT2FIX)
DEF_STRING_GETTER(reply_text, amq_client_connection)
DEF_CLIENT_CONNECTION_INT_GETTER(version_major, CHR2FIX)
DEF_CLIENT_CONNECTION_INT_GETTER(version_minor, CHR2FIX)

/////////////////////////////////////////////////////////////////////////////
//
// Functions for RWire::Session
//
/////////////////////////////////////////////////////////////////////////////

static VALUE rwire_amq_client_session_get_error_text(VALUE self)
{
    amq_client_session_t *session = NULL;
    Data_Get_Struct(self, amq_client_session_t, session);
    return rb_str_new2(session->error_text);
}

static VALUE rwire_amq_client_session_get_reply_text(VALUE self)
{
    amq_client_session_t *session = NULL;
    Data_Get_Struct(self, amq_client_session_t, session);
    return rb_str_new2(session->reply_text);
}

static VALUE rwire_amq_client_session_get_reply_code(VALUE self)
{
    amq_client_session_t *session = NULL;
    Data_Get_Struct(self, amq_client_session_t, session);
    return INT2FIX(session->reply_code);
}

static VALUE rwire_amq_client_session_get_queue(VALUE self)
{
    amq_client_session_t *session = NULL;
    Data_Get_Struct(self, amq_client_session_t, session);
    return rb_str_new2(session->queue);
}

static VALUE rwire_amq_client_session_get_exchange(VALUE self)
{
    amq_client_session_t *session = NULL;
    Data_Get_Struct(self, amq_client_session_t, session);
    return rb_str_new2(session->exchange);
}

static VALUE rwire_amq_client_session_get_message_count(VALUE self)
{
    amq_client_session_t *session = NULL;
    Data_Get_Struct(self, amq_client_session_t, session);
    return INT2FIX(session->message_count);
}

static VALUE rwire_amq_client_session_destroy(VALUE self)
{
    amq_client_session_t *session = NULL;
    Data_Get_Struct(self, amq_client_session_t, session);
    amq_client_session_destroy(&session);
    return Qnil;
}

static VALUE rwire_amq_client_session_wait(VALUE self, VALUE timeout)
{
    int result;
    amq_client_session_t *session = NULL;
    Data_Get_Struct(self, amq_client_session_t, session);
    if ( FIXNUM_P(timeout))
    {
      result = amq_client_session_wait(session, FIX2INT(timeout));
      return (INT2FIX(result));
    }
    else
    {
      rb_raise(rb_eTypeError, "Timeout was not an Fixnum");
    }
}

static VALUE rwire_amq_client_session_declare_exchange(
	VALUE self,
	VALUE exchange,
	VALUE type,
	VALUE passive,
	VALUE durable,
	VALUE undeletable,
	VALUE internal)
{
    char *_exchange = StringValuePtr(exchange);
    char *_type = StringValuePtr(type);
    amq_client_session_t *session = NULL;
    Data_Get_Struct(self, amq_client_session_t, session);
    amq_client_session_exchange_declare(session, 0,_exchange, _type, passive, durable, undeletable, internal, NULL);
    //TODO check for a more useful value to return
    return self;
}

static VALUE rwire_amq_client_session_declare_queue(VALUE self,
	VALUE queuename,
	VALUE passive,
	VALUE durable,
	VALUE exclusive,
	VALUE autodelete)
{

	char *_queuename              = NULL;
	amq_client_session_t *session = NULL;

	if (queuename != Qnil)
		_queuename = StringValuePtr(queuename);

	bool _passive    = (passive != Qfalse);
	bool _durable    = (durable != Qfalse);
	bool _exclusive  = (exclusive != Qfalse);
	bool _autodelete = (autodelete != Qfalse);

	Data_Get_Struct(self, amq_client_session_t, session);
	amq_client_session_queue_declare(session, 0,_queuename, _passive, _durable, _exclusive, _autodelete, NULL);
//TODO check for a more useful value to return
	return self;
}

static VALUE rwire_amq_client_session_bind_queue(VALUE self,
	VALUE queuename,
	VALUE exchange,
	VALUE routing_key)
{
    amq_client_session_t *session = NULL;
    char *_queuename = NULL;
    char *_exchange = StringValuePtr(exchange);
    char *_routing_key = StringValuePtr(routing_key);

	if (queuename != Qnil)
		_queuename = StringValuePtr(queuename);

    Data_Get_Struct(self, amq_client_session_t, session);
    amq_client_session_queue_bind(session, 0, _queuename, _exchange, _routing_key, NULL);
    //TODO check for a more useful value to return
    return self;
}

static VALUE rwire_amq_client_session_consume(VALUE self,
	VALUE queuename,
	VALUE consumer_tag,
	VALUE no_local,
	VALUE no_ack,
	VALUE exclusive)
{
	int result;
	amq_client_session_t *session = NULL;
	char *_queuename              = NULL;
	char *_consumer_tag           = NULL;
	bool _no_local   = (no_local != Qfalse);
	bool _no_ack     = (no_ack != Qfalse);
	bool _exclusive  = (exclusive != Qfalse);

	if (queuename != Qnil)
		_queuename = StringValuePtr(queuename);

	if (consumer_tag != Qnil)
		_consumer_tag = StringValuePtr(consumer_tag);


	Data_Get_Struct(self, amq_client_session_t, session);
	result = amq_client_session_basic_consume(session, 0, _queuename, _consumer_tag, _no_local, _no_ack, _exclusive, NULL);
//TODO check for a more useful value to return
	return self;
}

static VALUE rwire_amq_client_session_basic_cancel(VALUE self,
	VALUE consumer_tag)
{
	int result;
	amq_client_session_t *session = NULL;
	char *_consumer_tag           = NULL;

	if (consumer_tag != Qnil)
		_consumer_tag = StringValuePtr(consumer_tag);


	Data_Get_Struct(self, amq_client_session_t, session);
	result = amq_client_session_basic_cancel(session, _consumer_tag);
	fprintf(stderr, "amq_client_session_basic_cancel returns %d\n", result);
//TODO check for a more useful value to return
	return self;
}


static VALUE rwire_amq_client_session_publish_body(VALUE self,
	VALUE body,
	VALUE exchange,
	VALUE routing_key,
        VALUE r_mandatory,
        VALUE r_immediate,
        VALUE r_reply_to)
{
	char * exch = NULL;
	char * rkey = NULL;
	char * reply_to = NULL;

	bool mandatory = TO_BOOL(r_mandatory);
	bool immediate = TO_BOOL(r_immediate);

    	amq_client_session_t * session = NULL;
	amq_content_basic_t *  content = amq_content_basic_new();

	if (!NIL_P(exchange)) {
		exch = StringValuePtr(exchange);
	}

	if (!NIL_P(routing_key)) {
		rkey = StringValuePtr(routing_key);
	}

	if (!NIL_P(r_reply_to)) {
		reply_to = StringValuePtr(r_reply_to);
	}

	Data_Get_Struct(self, amq_client_session_t, session);

	int rc = 0;
	char * errmsg = NULL;

	do {
		char * blob = new_blob_from_rb_str(body);
		int    size = RSTRING_LEN(body);

		// Set the content body
		rc = amq_content_basic_set_body(content, blob, size, free);
		if (rc) {
			errmsg = "Unable to set content body";
			break;
		}

		// Set the reply_to field if passed in
		if (reply_to) {
			rc = amq_content_basic_set_reply_to(content, reply_to);
			if (rc) {
				errmsg = "Unable to set reply_to field";
				break;
			}

		}

		// Publish
		rc = amq_client_session_basic_publish(session, content, 0, exch, rkey, mandatory, immediate);
		if (rc) {
			errmsg = "Failed to publish message";
			break;
		}

	} while (false);

	if (content) {
		amq_content_basic_unlink(&content);
	}
	if (rc) {
		rb_raise(eAMQError, errmsg);
	}

	//TODO check for a more useful value to return
	return self;
}

static VALUE rwire_amq_client_session_publish_content(VALUE self,
	VALUE r_content,
	VALUE exchange,
	VALUE routing_key,
        VALUE r_mandatory,
        VALUE r_immediate)
{
	char * exch = NULL;
	char * rkey = NULL;

	bool mandatory = TO_BOOL(r_mandatory);
	bool immediate = TO_BOOL(r_immediate);

    	amq_client_session_t * session = NULL;
	amq_content_basic_t  * content = amq_content_basic_new();

	if (!NIL_P(exchange)) {
		exch = StringValuePtr(exchange);
	}

	if (!NIL_P(routing_key)) {
		rkey = StringValuePtr(routing_key);
	}

	Data_Get_Struct(self, amq_client_session_t, session);
	Data_Get_Struct(r_content, amq_content_basic_t, content);

	int rc = 0;
	do {
		rc = amq_client_session_basic_publish(session, content, 0, exch, rkey, mandatory, immediate);
		if (rc) {
			rb_raise(eAMQError, "Failed to publish message");
		}
	} while (false);

	return self;
}

static VALUE rwire_amq_client_session_basic_get(VALUE self, VALUE queuename)
{
	char *_queuename              = NULL;
	amq_client_session_t *session = NULL;

	if (queuename != Qnil)
		_queuename = StringValuePtr(queuename);

	Data_Get_Struct(self, amq_client_session_t, session);

	int rc = amq_client_session_basic_get(session,0, _queuename, 0);

	if (rc)
	{
		rb_raise(eAMQError, "Failed to basic get");
	}

	return self;
}

static VALUE rwire_amq_client_session_get_basic_arrived(VALUE self)
{
	VALUE rb_content;
	amq_client_session_t * session = NULL;
	amq_content_basic_t  * content = NULL;

	Data_Get_Struct(self, amq_client_session_t, session);

	content = amq_client_session_basic_arrived(session);

	if (content)
	{
		rb_content = Data_Wrap_Struct(cContent, 0, rwire_amq_content_basic_free, content);
		return rb_content;
	}
	else
		return Qnil;

}

static VALUE rwire_amq_client_session_get_basic_arrived_count(VALUE self)
{
	amq_client_session_t * session = NULL;

	Data_Get_Struct(self, amq_client_session_t, session);

	int rc = amq_client_session_get_basic_arrived_count(session);

	return INT2FIX(rc);
}

static VALUE rwire_amq_client_session_get_basic_returned(VALUE self)
{
	VALUE rb_content;
	amq_client_session_t * session = NULL;
	amq_content_basic_t  * content = NULL;

	Data_Get_Struct(self, amq_client_session_t, session);

	content = amq_client_session_basic_returned(session);

	if (content)
	{
		rb_content = Data_Wrap_Struct(cContent, 0, rwire_amq_content_basic_free, content);
		return rb_content;
	}
	else
		return Qnil;

}

static VALUE rwire_amq_client_session_get_basic_returned_count(VALUE self)
{
	amq_client_session_t * session = NULL;

	Data_Get_Struct(self, amq_client_session_t, session);

	int rc = amq_client_session_get_basic_returned_count(session);

	return INT2FIX(rc);
}


static VALUE rwire_amq_client_session_get_consumer_tag(VALUE self)
{
	amq_client_session_t * session = NULL;

	Data_Get_Struct(self, amq_client_session_t, session);

	char * tag = amq_client_session_get_consumer_tag(session);

	return rb_str_new2(tag);
}

static VALUE rwire_amq_client_session_get_alive(VALUE self)
{
	amq_client_session_t * session = NULL;

	Data_Get_Struct(self, amq_client_session_t, session);

	bool alive = amq_client_session_get_alive(session);

	return (alive ? Qtrue : Qfalse);
}


/////////////////////////////////////////////////////////////////////////////
//
// MACROS for helping defining attribute methods in Init entry function
//
/////////////////////////////////////////////////////////////////////////////

#define RB_DEF_METHOD(klass, name, prefix, num_args) \
	rb_define_method(klass, #name, prefix##_##name, num_args)

#define RB_DEF_GETTER(klass, prefix, name) \
	rb_define_method(klass, #name, prefix##_get_##name, 0)

#define RB_DEF_BOOL_GETTER(klass, prefix, name) \
	rb_define_method(klass, #name "?", prefix##_get_##name, 0); \
	RB_DEF_GETTER(klass, prefix, name)

#define RB_DEF_SETTER(klass, prefix, name) \
	rb_define_method(klass, #name "=", prefix##_set_##name, 1)

#define RB_DEF_ATTR(klass, prefix, name) \
	RB_DEF_GETTER(klass, prefix, name);\
	RB_DEF_SETTER(klass, prefix, name)

#define RB_DEF_CONTENT_GETTER(name) \
	RB_DEF_GETTER(cContent, rwire_amq_content_basic, name)

#define RB_DEF_CONTENT_SETTER(name) \
	RB_DEF_SETTER(cContent, rwire_amq_content_basic, name)

#define RB_DEF_CONTENT_ATTR(name) \
	RB_DEF_CONTENT_GETTER(name);\
	RB_DEF_CONTENT_SETTER(name)

#define RB_DEF_CONN_GETTER(name) \
	RB_DEF_GETTER(cConnection, rwire_amq_client_connection, name)

#define RB_DEF_CONN_BOOL_GETTER(name) \
	RB_DEF_BOOL_GETTER(cConnection, rwire_amq_client_connection, name)

#define RB_DEF_CONN_SETTER(name) \
	RB_DEF_SETTER(cConnection, rwire_amq_client_connection, name)

#define RB_DEF_CONN_ATTR(name) \
	RB_DEF_CONN_GETTER(name);\
	RB_DEF_CONN_SETTER(name)

#define RB_DEF_CONN_BOOL_ATTR(name) \
	RB_DEF_CONN_BOOL_GETTER(name);\
	RB_DEF_CONN_SETTER(name)

#define RB_DEF_SESS_METHOD(name, num_args) \
	RB_DEF_METHOD(cSession, name, rwire_amq_client_session, num_args)

#define RB_DEF_SESS_GETTER(name) \
	RB_DEF_GETTER(cSession, rwire_amq_client_session, name)

#define RB_DEF_SESS_BOOL_GETTER(name) \
	RB_DEF_BOOL_GETTER(cSession, rwire_amq_client_session, name)

#define RB_DEF_SESS_SETTER(name) \
	RB_DEF_SETTER(cSession, rwire_amq_client_session, name)

#define RB_DEF_SESS_ATTR(name) \
	RB_DEF_SESS_GETTER(name);\
	RB_DEF_SESS_SETTER(name)

#define RB_DEF_SESS_BOOL_ATTR(name) \
	RB_DEF_SESS_BOOL_GETTER(name);\
	RB_DEF_SESS_SETTER(name)

/////////////////////////////////////////////////////////////////////////////
//
// Ruby Extension Definition
//
/////////////////////////////////////////////////////////////////////////////

void Init_rwire()
{
	icl_system_initialise(0, NULL);


	cRWire      = rb_define_module("RWire");
	cConnection = rb_define_class_under(cRWire, "Connection", rb_cObject);
	cSession    = rb_define_class_under(cRWire, "Session",    rb_cObject);
	cContent    = rb_define_class_under(cRWire, "Content",    rb_cObject);
	eAMQError   = rb_define_class("AMQError", rb_eRuntimeError);
	eAMQDestroyedError = rb_define_class("AMQDestroyedError", eAMQError);

	// RWire
	rb_define_method(cRWire, "initialize", rwire_init, 1); //initialize(trace_levoel)

	// Content
	rb_define_alloc_func(cContent, rwire_amq_content_basic_alloc);
	rb_define_method(cContent, "unlink",  rwire_amq_content_basic_unlink, 0);

	RB_DEF_CONTENT_ATTR(body);
	RB_DEF_CONTENT_ATTR(message_id);

	RB_DEF_CONTENT_ATTR(reply_to);

	RB_DEF_CONTENT_GETTER(exchange);
	RB_DEF_CONTENT_GETTER(routing_key);

	// TODO: Set routing key requires multiple arguments
	// Not terribly useful since routing key is usually set when sending message
	//rb_define_method(cContent, "routing_key=", rwire_amq_content_basic_set_routing_key, 1);

	RB_DEF_CONTENT_ATTR(content_type);
	RB_DEF_CONTENT_ATTR(content_encoding);
	RB_DEF_CONTENT_ATTR(correlation_id);
	RB_DEF_CONTENT_ATTR(expiration);
	RB_DEF_CONTENT_ATTR(user_id);
	RB_DEF_CONTENT_ATTR(app_id);

	RB_DEF_CONTENT_GETTER(body_size);
	RB_DEF_CONTENT_GETTER(class_id);
	RB_DEF_CONTENT_ATTR(priority);
	RB_DEF_CONTENT_ATTR(delivery_mode);
	RB_DEF_CONTENT_ATTR(timestamp);


	// No support for headers yet.	Need more thoughts on this. Should
	// probably expose this separately

	//rb_define_method(cContent, "headers", rwire_amq_content_basic_get_headers, 0);
	//rb_define_method(cContent, "content_basic_set_headers", rwire_amq_content_basic_set_headers, 0);

	// AMQ message type.	Is it even useful to expose it?	If do, needs to pick a
	// different name to avoid conflict with the Ruby type method

	//rb_define_method(cContent, "type", rwire_amq_content_basic_get_type, 0);
	//rb_define_method(cContent, "content_basic_set_type", rwire_amq_content_basic_set_type, 0);

// Connection
	rb_define_alloc_func(cConnection, rwire_connection_alloc);
	rb_define_method(cConnection, "initialize", rwire_connection_init, 7);
	rb_define_method(cConnection, "session_new", rwire_amq_client_session_new, 0);

	// rb_define_method(cConnection, "wait", rwire_amq_client_connection_wait, 1);
	// rb_define_method(cConnection, "selftest", rwire_amq_client_connection_selftest, 0);

	rb_define_method(cConnection, "destroy", rwire_connection_destroy, 0);

	RB_DEF_CONN_BOOL_ATTR(silent);
	RB_DEF_CONN_BOOL_GETTER(alive);
	RB_DEF_CONN_GETTER(channel_max);
	RB_DEF_CONN_GETTER(class_id);
	RB_DEF_CONN_GETTER(error_text);
	RB_DEF_CONN_GETTER(frame_max);
	RB_DEF_CONN_GETTER(heartbeat);
	RB_DEF_CONN_GETTER(known_hosts);
	RB_DEF_CONN_GETTER(method_id);
	RB_DEF_CONN_GETTER(reply_code);
	RB_DEF_CONN_GETTER(reply_text);
	RB_DEF_CONN_GETTER(version_major);
	RB_DEF_CONN_GETTER(version_minor);

// Session
	RB_DEF_SESS_METHOD(destroy, 0);
	RB_DEF_SESS_METHOD(wait, 1); // timeout
	RB_DEF_SESS_GETTER(basic_arrived);
	RB_DEF_SESS_GETTER(basic_arrived_count);
	RB_DEF_SESS_GETTER(basic_returned);
	RB_DEF_SESS_GETTER(basic_returned_count);
	RB_DEF_SESS_BOOL_GETTER(alive);

	//RB_DEF_SESS_METHOD(channel_flow, 0);
	//RB_DEF_SESS_METHOD(access_request, 0);
	RB_DEF_SESS_METHOD(declare_exchange, 6);
	//RB_DEF_SESS_METHOD(exchange_delete, 0);
	RB_DEF_SESS_METHOD(declare_queue, 5);
	RB_DEF_SESS_METHOD(bind_queue, 3);
	//RB_DEF_SESS_METHOD(queue_unbind, 0);
	//RB_DEF_SESS_METHOD(queue_purge, 0);
	//RB_DEF_SESS_METHOD(queue_delete, 0);
	RB_DEF_SESS_METHOD(consume, 5);
	RB_DEF_SESS_METHOD(basic_cancel, 1);
	RB_DEF_SESS_METHOD(publish_body, 6);
	RB_DEF_SESS_METHOD(publish_content, 5);
	//RB_DEF_SESS_METHOD(basic_ack, 0);
	//RB_DEF_SESS_METHOD(basic_reject, 0);
	RB_DEF_SESS_METHOD(basic_get, 1);

	//RB_DEF_SESS_BOOL_GETTER(silent);
	RB_DEF_SESS_GETTER(error_text);
	//RB_DEF_SESS_GETTER(ticket);
	RB_DEF_SESS_GETTER(queue);
	RB_DEF_SESS_GETTER(exchange);
	RB_DEF_SESS_GETTER(message_count);
	//RB_DEF_SESS_GETTER(consumer_count);
	//RB_DEF_SESS_BOOL_GETTER(active);
	RB_DEF_SESS_GETTER(reply_text);
	RB_DEF_SESS_GETTER(reply_code);
	RB_DEF_SESS_GETTER(consumer_tag);
	//RB_DEF_SESS_GETTER(routing_key);
	//RB_DEF_SESS_GETTER(scope);
	//RB_DEF_SESS_GETTER(delivery_tag);
	//RB_DEF_SESS_BOOL_GETTER(redelivered);
}
