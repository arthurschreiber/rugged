/*
 * The MIT License
 *
 * Copyright (c) 2014 GitHub, Inc
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "rugged.h"

extern VALUE rb_mRugged;
extern VALUE rb_cRuggedRepo;

extern VALUE rb_cRuggedCredPlaintext;
extern VALUE rb_cRuggedCredSshKey;
extern VALUE rb_cRuggedCredDefault;

VALUE rb_cRuggedRemote;

static void rb_git_remote__free(git_remote *remote)
{
	git_remote_free(remote);
}

VALUE rugged_remote_new(VALUE klass, VALUE owner, git_remote *remote)
{
	VALUE rb_remote;

	rb_remote = Data_Wrap_Struct(klass, NULL, &rb_git_remote__free, remote);
	rugged_set_owner(rb_remote, owner);
	return rb_remote;
}

static inline void rugged_validate_remote_url(VALUE rb_url)
{
	Check_Type(rb_url, T_STRING);
	if (!git_remote_valid_url(StringValueCStr(rb_url)))
		rb_raise(rb_eArgError, "Invalid URL format");
}

/*
 *  call-seq:
 *    Remote.new(repository, url) -> remote
 *
 *  Return a new remote with +url+ in +repository+ , the remote is not persisted:
 *  - +url+: a valid remote url
 *
 *  Returns a new Rugged::Remote object
 *
 *    Rugged::Remote.new(@repo, 'git://github.com/libgit2/libgit2.git') #=> #<Rugged::Remote:0x00000001fbfa80>
 */
static VALUE rb_git_remote_new(VALUE klass, VALUE rb_repo, VALUE rb_url)
{
	git_remote *remote;
	git_repository *repo;
	int error;

	rugged_check_repo(rb_repo);
	rugged_validate_remote_url(rb_url);

	Data_Get_Struct(rb_repo, git_repository, repo);

	error = git_remote_create_inmemory(
			&remote,
			repo,
			NULL,
			StringValueCStr(rb_url));

	rugged_exception_check(error);

	return rugged_remote_new(klass, rb_repo, remote);
}

/*
 *  call-seq:
 *     Remote.add(repository, name, url) -> remote
 *
 *  Add a new remote with +name+ and +url+ to +repository+
 *  - +url+: a valid remote url
 *  - +name+: a valid remote name
 *
 *  Returns a new Rugged::Remote object
 *
 *    Rugged::Remote.add(@repo, 'origin', 'git://github.com/libgit2/rugged.git') #=> #<Rugged::Remote:0x00000001fbfa80>
 */
static VALUE rb_git_remote_add(VALUE klass, VALUE rb_repo,VALUE rb_name, VALUE rb_url)
{
	git_remote *remote;
	git_repository *repo;
	int error;

	Check_Type(rb_name, T_STRING);
	rugged_validate_remote_url(rb_url);
	rugged_check_repo(rb_repo);

	Data_Get_Struct(rb_repo, git_repository, repo);

	error = git_remote_create(
			&remote,
			repo,
			StringValueCStr(rb_name),
			StringValueCStr(rb_url));

	rugged_exception_check(error);

	return rugged_remote_new(klass, rb_repo, remote);
}

/*
 *  call-seq:
 *    Remote.lookup(repository, name) -> remote or nil
 *
 *  Return an existing remote with +name+ in +repository+:
 *  - +name+: a valid remote name
 *
 *  Returns a new Rugged::Remote object or +nil+ if the
 *  remote doesn't exist
 *
 *    Rugged::Remote.lookup(@repo, 'origin') #=> #<Rugged::Remote:0x00000001fbfa80>
 */
static VALUE rb_git_remote_lookup(VALUE klass, VALUE rb_repo, VALUE rb_name)
{
	git_remote *remote;
	git_repository *repo;
	int error;

	Check_Type(rb_name, T_STRING);
	rugged_check_repo(rb_repo);
	Data_Get_Struct(rb_repo, git_repository, repo);

	error = git_remote_load(&remote, repo, StringValueCStr(rb_name));

	if (error == GIT_ENOTFOUND)
		return Qnil;

	rugged_exception_check(error);

	return rugged_remote_new(klass, rb_repo, remote);
}

static VALUE rugged_rhead_new(const git_remote_head *head)
{
	VALUE rb_head = rb_hash_new();

	rb_hash_aset(rb_head, CSTR2SYM("local?"), head->local ? Qtrue : Qfalse);
	rb_hash_aset(rb_head, CSTR2SYM("oid"), rugged_create_oid(&head->oid));
	rb_hash_aset(rb_head, CSTR2SYM("loid"),
			git_oid_iszero(&head->loid) ? Qnil : rugged_create_oid(&head->loid));
	rb_hash_aset(rb_head, CSTR2SYM("name"), rb_str_new_utf8(head->name));

	return rb_head;
}

/*
 *  call-seq:
 *    remote.ls() -> an_enumerator
 *    remote.ls() { |remote_head_hash| block }
 *
 *  List references available in a connected +remote+ repository along
 *  with the associated commit IDs.
 *
 *  Call the given block once for each remote head in the +remote+ as a
 *  +Hash+.
 *  If no block is given an Enumerator is returned.
 *
 *    remote.ls.to_a #=> [{:local?=>false, :oid=>"b3ee97a91b02e91c35394950bda6ea622044baad", :loid=> nil, :name=>"refs/heads/development"}]
 *
 *  remote head hash includes:
 *  [:oid] oid of the remote head
 *  [:name] name of the remote head
 *
 *
 */
static VALUE rb_git_remote_ls(VALUE self)
{
	int error, exception = 0;
	git_remote *remote;
	const git_remote_head **heads;
	size_t heads_len, i;
	Data_Get_Struct(self, git_remote, remote);

	if (!rb_block_given_p())
		return rb_funcall(self, rb_intern("to_enum"), 1, CSTR2SYM("ls"));

	error = git_remote_connect(remote, GIT_DIRECTION_FETCH);
	rugged_exception_check(error);

	error = git_remote_ls(&heads, &heads_len, remote);

	for (i = 0; i < heads_len && !exception; i++) {
		rb_protect(rb_yield, rugged_rhead_new(heads[i]), &exception);
	}

	git_remote_disconnect(remote);

	if (exception)
		rb_jump_tag(exception);

	rugged_exception_check(error);

	return Qnil;
}

/*
 *  call-seq:
 *    remote.name() -> string
 *
 *	Returns the remote's name
 *	  remote.name #=> "origin"
 */
static VALUE rb_git_remote_name(VALUE self)
{
	git_remote *remote;
	const char * name;
	Data_Get_Struct(self, git_remote, remote);

	name = git_remote_name(remote);

	return name ? rb_str_new_utf8(name) : Qnil;
}

/*
 *  call-seq:
 *    remote.url() -> string
 *
 *  Returns the remote's url
 *    remote.url #=> "git://github.com/libgit2/rugged.git"
 */
static VALUE rb_git_remote_url(VALUE self)
{
	git_remote *remote;
	Data_Get_Struct(self, git_remote, remote);

	return rb_str_new_utf8(git_remote_url(remote));
}

/*
 *  call-seq:
 *    remote.url = url -> url
 *
 *  Sets the remote's url without persisting it in the config.
 *  Existing connections will not be updated.
 *    remote.url = 'git://github.com/libgit2/rugged.git' #=> "git://github.com/libgit2/rugged.git"
 */
static VALUE rb_git_remote_set_url(VALUE self, VALUE rb_url)
{
	git_remote *remote;

	rugged_validate_remote_url(rb_url);
	Data_Get_Struct(self, git_remote, remote);

	rugged_exception_check(
		git_remote_set_url(remote, StringValueCStr(rb_url))
	);
	return rb_url;
}

/*
 *  call-seq:
 *    remote.push_url() -> string or nil
 *
 *  Returns the remote's url for pushing or nil if no special url for
 *  pushing is set.
 *    remote.push_url #=> "git://github.com/libgit2/rugged.git"
 */
static VALUE rb_git_remote_push_url(VALUE self)
{
	git_remote *remote;
	const char * push_url;

	Data_Get_Struct(self, git_remote, remote);

	push_url = git_remote_pushurl(remote);
	return push_url ? rb_str_new_utf8(push_url) : Qnil;
}

/*
 *  call-seq:
 *    remote.push_url = url -> url
 *
 *  Sets the remote's url for pushing without persisting it in the config.
 *  Existing connections will not be updated.
 *    remote.push_url = 'git@github.com/libgit2/rugged.git' #=> "git@github.com/libgit2/rugged.git"
 */
static VALUE rb_git_remote_set_push_url(VALUE self, VALUE rb_url)
{
	git_remote *remote;

	rugged_validate_remote_url(rb_url);
	Data_Get_Struct(self, git_remote, remote);

	rugged_exception_check(
		git_remote_set_pushurl(remote, StringValueCStr(rb_url))
	);

	return rb_url;
}

static VALUE rb_git_remote_refspecs(VALUE self, git_direction direction)
{
	git_remote *remote;
	int error = 0;
	git_strarray refspecs;
	VALUE rb_refspec_array;

	Data_Get_Struct(self, git_remote, remote);

	if (direction == GIT_DIRECTION_FETCH)
		error = git_remote_get_fetch_refspecs(&refspecs, remote);
	else
		error = git_remote_get_push_refspecs(&refspecs, remote);

	rugged_exception_check(error);

	rb_refspec_array = rugged_strarray_to_rb_ary(&refspecs);
	git_strarray_free(&refspecs);
	return rb_refspec_array;
}

/*
 *  call-seq:
 *  remote.fetch_refspecs -> array
 *
 *  Get the remote's list of fetch refspecs as +array+
 */
static VALUE rb_git_remote_fetch_refspecs(VALUE self)
{
	return rb_git_remote_refspecs(self, GIT_DIRECTION_FETCH);
}

/*
 *  call-seq:
 *  remote.push_refspecs -> array
 *
 *  Get the remote's list of push refspecs as +array+
 */
static VALUE rb_git_remote_push_refspecs(VALUE self)
{
	return rb_git_remote_refspecs(self, GIT_DIRECTION_PUSH);
}

static VALUE rb_git_remote_add_refspec(VALUE self, VALUE rb_refspec, git_direction direction)
{
	git_remote *remote;
	int error = 0;

	Data_Get_Struct(self, git_remote, remote);

	Check_Type(rb_refspec, T_STRING);

	if (direction == GIT_DIRECTION_FETCH)
		error = git_remote_add_fetch(remote, StringValueCStr(rb_refspec));
	else
		error = git_remote_add_push(remote, StringValueCStr(rb_refspec));

	rugged_exception_check(error);

	return Qnil;
}

/*
 *  call-seq:
 *    remote.add_fetch(refspec) -> nil
 *
 *  Add a fetch refspec to the remote
 */
static VALUE rb_git_remote_add_fetch(VALUE self, VALUE rb_refspec)
{
	return rb_git_remote_add_refspec(self, rb_refspec, GIT_DIRECTION_FETCH);
}

/*
 *  call-seq:
 *    remote.add_push(refspec) -> nil
 *
 *  Add a push refspec to the remote
 */
static VALUE rb_git_remote_add_push(VALUE self, VALUE rb_refspec)
{
	return rb_git_remote_add_refspec(self, rb_refspec, GIT_DIRECTION_PUSH);
}

/*
 *  call-seq:
 *    remote.clear_refspecs -> nil
 *
 *  Remove all configured fetch and push refspecs from the remote.
 */
static VALUE rb_git_remote_clear_refspecs(VALUE self)
{
	git_remote *remote;

	Data_Get_Struct(self, git_remote, remote);

	git_remote_clear_refspecs(remote);

	return Qnil;
}

/*
 *  call-seq:
 *    Remote.names(repository) -> array
 *
 *  Returns the names of all remotes in +repository+
 *
 *    Rugged::Remote.names(@repo) #=> ['origin', 'upstream']
 */

static VALUE rb_git_remote_names(VALUE klass, VALUE rb_repo)
{
	git_repository *repo;
	git_strarray remotes;
	VALUE rb_remote_names;
	int error;

	rugged_check_repo(rb_repo);

	Data_Get_Struct(rb_repo, git_repository, repo);

	error = git_remote_list(&remotes, repo);
	rugged_exception_check(error);

	rb_remote_names = rugged_strarray_to_rb_ary(&remotes);
	git_strarray_free(&remotes);
	return rb_remote_names;
}

/* :nodoc: */
static VALUE rb_git_remote_each(VALUE klass, VALUE rb_repo)
{
	git_repository *repo;
	git_strarray remotes;
	size_t i;
	int error = 0;
	int exception = 0;

	if (!rb_block_given_p())
		return rb_funcall(klass, rb_intern("to_enum"), 2, CSTR2SYM("each"), rb_repo);

	rugged_check_repo(rb_repo);
	Data_Get_Struct(rb_repo, git_repository, repo);

	error = git_remote_list(&remotes, repo);
	rugged_exception_check(error);

	for (i = 0; !exception && !error && i < remotes.count; ++i) {
		git_remote *remote;
		error = git_remote_load(&remote, repo, remotes.strings[i]);

		if (!error) {
			rb_protect(
				rb_yield, rugged_remote_new(klass, rb_repo, remote),
				&exception);
		}
	}

	git_strarray_free(&remotes);

	if (exception)
		rb_jump_tag(exception);

	rugged_exception_check(error);
	return Qnil;
}

/*
 *  call-seq:
 *    remote.save -> true
 *
 *  Saves the remote data ( url, fetchspecs, ...) to the config
 *
 *  One can't save a in-memory remote created with Remote.new.
 *  Doing so will result in an exception being raised.
*/
static VALUE rb_git_remote_save(VALUE self)
{
	git_remote *remote;

	Data_Get_Struct(self, git_remote, remote);

	rugged_exception_check(
		git_remote_save(remote)
	);
	return Qtrue;
}

static int cb_remote__rename_problem(const char* refspec_name, void *payload)
{
	rb_ary_push((VALUE) payload, rb_str_new_utf8(refspec_name));
	return 0;
}

/*
 *  call-seq:
 *    remote.rename!(new_name) -> array or nil
 *
 *  Renames a remote
 *
 *  All remote-tracking branches and configuration settings
 *  for the remote are updated.
 *
 *  Returns +nil+ if everything was updated or array of fetch refspecs
 *  that haven't been automatically updated and need potential manual
 *  tweaking.
 *
 *  A temporary in-memory remote, created with Remote.new
 *  cannot be given a name with this method.
 *    remote = Rugged::Remote.lookup(@repo, 'origin')
 *    remote.rename!('upstream') #=> nil
 *
*/
static VALUE rb_git_remote_rename(VALUE self, VALUE rb_new_name)
{
	git_remote *remote;
	int error = 0;
	VALUE rb_refspec_ary = rb_ary_new();

	Check_Type(rb_new_name, T_STRING);
	Data_Get_Struct(self, git_remote, remote);
	error = git_remote_rename(
			remote,
			StringValueCStr(rb_new_name),
			cb_remote__rename_problem, (void *)rb_refspec_ary);

	rugged_exception_check(error);

	return RARRAY_LEN(rb_refspec_ary) == 0 ? Qnil : rb_refspec_ary;
}

static int rugged__remote_transfer_progress_cb(const git_transfer_progress *stats, void *payload)
{
	struct rugged_remote_cb_payload *remote_payload = payload;
	VALUE args = rb_ary_new2(5);

	rb_ary_push(args, remote_payload->transfer_progress);
	rb_ary_push(args, UINT2NUM(stats->total_objects));
	rb_ary_push(args, UINT2NUM(stats->indexed_objects));
	rb_ary_push(args, UINT2NUM(stats->received_objects));
	rb_ary_push(args, UINT2NUM(stats->local_objects));
	rb_ary_push(args, UINT2NUM(stats->total_deltas));
	rb_ary_push(args, UINT2NUM(stats->indexed_deltas));
	rb_ary_push(args, INT2FIX(stats->received_bytes));

	rb_protect(rugged__block_yield_splat, args, &remote_payload->exception);

	return remote_payload->exception ? GIT_ERROR : GIT_OK;
}

static int rugged__remote_progress_cb(const char *str, int len, void *payload)
{
	struct rugged_remote_cb_payload *remote_payload = payload;
	VALUE args = rb_ary_new2(2);

	rb_ary_push(args, remote_payload->progress);
	rb_ary_push(args, rb_str_new(str, len));

	rb_protect(rugged__block_yield_splat, args, &remote_payload->exception);

	return remote_payload->exception ? GIT_ERROR : GIT_OK;
}

static int rugged__remote_update_tips_cb(const char *refname, const git_oid *src, const git_oid *dest, void *payload)
{
	struct rugged_remote_cb_payload *remote_payload = payload;
	VALUE args = rb_ary_new2(4);

	rb_ary_push(args, remote_payload->update_tips);
	rb_ary_push(args, rb_str_new_utf8(refname));
	rb_ary_push(args, git_oid_iszero(src) ? Qnil : rugged_create_oid(src));
	rb_ary_push(args, git_oid_iszero(dest) ? Qnil : rugged_create_oid(dest));

	rb_protect(rugged__block_yield_splat, args, &remote_payload->exception);

	return remote_payload->exception ? GIT_ERROR : GIT_OK;
}

struct extract_cred_payload
{
	VALUE rb_cred;
	git_cred **cred;
	unsigned int allowed_types;
};

static VALUE rugged__extract_cred(VALUE payload) {
	struct extract_cred_payload *cred_payload = (struct extract_cred_payload*)payload;
	git_cred **cred = cred_payload->cred;
	VALUE rb_cred = cred_payload->rb_cred;

	if (rb_obj_is_kind_of(rb_cred, rb_cRuggedCredPlaintext)) {
		if (!(cred_payload->allowed_types & GIT_CREDTYPE_USERPASS_PLAINTEXT)) {
			rb_raise(rb_eArgError, "Invalid credential type");
		} else {
			VALUE rb_username = rb_iv_get(rb_cred, "@username");
			VALUE rb_password = rb_iv_get(rb_cred, "@password");

			Check_Type(rb_username, T_STRING);
			Check_Type(rb_password, T_STRING);


			rugged_exception_check(
				git_cred_userpass_plaintext_new(cred,
					StringValueCStr(rb_username), StringValueCStr(rb_password)));
		}
	} else if (rb_obj_is_kind_of(rb_cred, rb_cRuggedCredSshKey)) {
		if (!(cred_payload->allowed_types & GIT_CREDTYPE_SSH_KEY)) {
			rb_raise(rb_eArgError, "Invalid credential type");
		} else {
			VALUE rb_username   = rb_iv_get(rb_cred, "@username");
			VALUE rb_publickey  = rb_iv_get(rb_cred, "@publickey");
			VALUE rb_privatekey = rb_iv_get(rb_cred, "@privatekey");
			VALUE rb_passphrase = rb_iv_get(rb_cred, "@passphrase");

			Check_Type(rb_privatekey, T_STRING);

			if (!NIL_P(rb_username))
				Check_Type(rb_username, T_STRING);
			if (!NIL_P(rb_publickey))
				Check_Type(rb_publickey, T_STRING);
			if (!NIL_P(rb_passphrase))
				Check_Type(rb_passphrase, T_STRING);

			rugged_exception_check(
				git_cred_ssh_key_new(cred,
					NIL_P(rb_username) ? NULL : StringValueCStr(rb_username),
					NIL_P(rb_publickey) ? NULL : StringValueCStr(rb_publickey),
					StringValueCStr(rb_privatekey),
					NIL_P(rb_passphrase) ? NULL : StringValueCStr(rb_passphrase)));
		}
	} else if (rb_obj_is_kind_of(rb_cred, rb_cRuggedCredDefault)) {
		if (!(cred_payload->allowed_types & GIT_CREDTYPE_SSH_KEY)) {
			rb_raise(rb_eArgError, "Invalid credential type");
		} else {
			rugged_exception_check(git_cred_default_new(cred));
		}
	}

	return Qnil;
}

static int rugged__remote_credentials_cb(
	git_cred **cred,
	const char *url,
	const char *username_from_url,
	unsigned int allowed_types,
	void *payload)
{
	struct rugged_remote_cb_payload *remote_payload = payload;
	struct extract_cred_payload cred_payload;
	VALUE args = rb_ary_new2(4), rb_allowed_types = rb_ary_new();

	if (allowed_types & GIT_CREDTYPE_USERPASS_PLAINTEXT)
		rb_ary_push(rb_allowed_types, CSTR2SYM("plaintext"));

	if (allowed_types & GIT_CREDTYPE_SSH_KEY)
		rb_ary_push(rb_allowed_types, CSTR2SYM("ssh_key"));

	if (allowed_types & GIT_CREDTYPE_DEFAULT)
		rb_ary_push(rb_allowed_types, CSTR2SYM("default"));

	rb_ary_push(args, remote_payload->credentials);
	rb_ary_push(args, url ? rb_str_new2(url) : Qnil);
	rb_ary_push(args, username_from_url ? rb_str_new2(username_from_url) : Qnil);
	rb_ary_push(args, rb_allowed_types);

	cred_payload.cred = cred;
	cred_payload.rb_cred = rb_protect(rugged__block_yield_splat, args, &remote_payload->exception);
	cred_payload.allowed_types = allowed_types;

	if (!remote_payload->exception)
		rb_protect(rugged__extract_cred, (VALUE)&cred_payload, &remote_payload->exception);

	return remote_payload->exception ? GIT_ERROR : GIT_OK;
}

static int rugged__default_remote_credentials_cb(
	git_cred **cred,
	const char *url,
	const char *username_from_url,
	unsigned int allowed_types,
	void *payload)
{
	struct rugged_remote_cb_payload *remote_payload = payload;
	struct extract_cred_payload cred_payload;

	cred_payload.cred = cred;
	cred_payload.rb_cred = remote_payload->credentials;
	cred_payload.allowed_types = allowed_types;

	rb_protect(rugged__extract_cred, (VALUE)&cred_payload, &remote_payload->exception);

	return remote_payload->exception ? GIT_ERROR : GIT_OK;
}

void parse_fetch_options(git_remote_callbacks *callbacks, VALUE rb_options_hash, struct rugged_remote_cb_payload *payload)
{
	VALUE val;

	if (NIL_P(rb_options_hash))
		return;

	val = rb_hash_aref(rb_options_hash, CSTR2SYM("credentials"));
	if (RTEST(val)) {
		if (rb_obj_is_kind_of(val, rb_cRuggedCredPlaintext) ||
			rb_obj_is_kind_of(val, rb_cRuggedCredSshKey) ||
			rb_obj_is_kind_of(val, rb_cRuggedCredDefault))
		{
			callbacks->credentials = rugged__default_remote_credentials_cb;
			payload->credentials = val;
		} else if (rb_respond_to(val, rb_intern("call"))) {
			callbacks->credentials = rugged__remote_credentials_cb;
			payload->credentials = val;
		} else {
			rb_raise(rb_eArgError,
				"Expected a Rugged::Credentials type, a Proc or an object that responds to call (:credentials).");
		}
	}

	val = rb_hash_aref(rb_options_hash, CSTR2SYM("progress"));
	if (RTEST(val)) {
		if (!rb_respond_to(val, rb_intern("call"))) {
			rb_raise(rb_eArgError, "Expected a Proc or an object that responds to call (:progress).");
		}

		payload->progress = val;
		callbacks->progress = rugged__remote_progress_cb;
	}

	val = rb_hash_aref(rb_options_hash, CSTR2SYM("transfer_progress"));
	if (RTEST(val)) {
		if (!rb_respond_to(val, rb_intern("call"))) {
			rb_raise(rb_eArgError, "Expected a Proc or an object that responds to call (:transfer_progress).");
		}

		payload->transfer_progress = val;
		callbacks->transfer_progress = rugged__remote_transfer_progress_cb;
	}

	val = rb_hash_aref(rb_options_hash, CSTR2SYM("update_tips"));
	if (RTEST(val)) {
		if (!rb_respond_to(val, rb_intern("call"))) {
			rb_raise(rb_eArgError, "Expected a Proc or an object that responds to call (:update_tips).");
		}

		payload->update_tips = val;
		callbacks->update_tips = rugged__remote_update_tips_cb;
	}

	callbacks->payload = payload;
}

/*
 *  call-seq:
 *    remote.fetch(refspecs = nil, options = {}) -> nil
 *
 *  Download new data from +remote+ and update tips.
 *
 *  Connects to +remote+, downloads data, disconnects, and updates the local remote-tracking
 *  branches.
 *
 *  You can optionally pass in an alternative list of +refspecs+ to use instead of the fetch
 *  refspecs setup for +remote+.
 *
 *  The following options can be passed in the +options+ Hash:
 *
 *  :credentials ::
 *    The credentials to use for the fetch operation. Can be either an instance of one
 *    of the Rugged::Credentials types, or a proc returning one of the former.
 *    The proc will be called with the +url+, the +username+ from the url (if applicable) and
 *    a list of applicable credential types.
 *
 *  :progress ::
 *    A callback that will be executed with the textual progress received from the remote.
 *    This is the text send over the progress side-band (ie. the "counting objects" output).
 *
 *  :transfer_progress ::
 *    A callback that will be executed to report clone progress information. It will be passed
 *    the amount of +total_objects+, +indexed_objects+, +received_objects+ and +received_bytes+.
 *
 *  :update_tips ::
 *    A callback that will be executed each time a reference is updated locally. It will be
 *    passed the +refname+, +old_oid+ and +new_oid+.
 *
 *  Example:
 *
 *    remote = Rugged::Remote.lookup(@repo, 'origin')
 *    remote.fetch({
 *      :transfer_progress => lambda { |total_objects, indexed_objects, received_objects, received_bytes|
 *        # ...
 *      }
 *    })
 */
static VALUE rb_git_remote_fetch(int argc, VALUE *argv, VALUE self) {
	VALUE rb_options, rb_refspecs;
	git_remote *remote, *tmp_remote;
	int error, i;
	git_remote_callbacks callbacks = GIT_REMOTE_CALLBACKS_INIT;
	struct rugged_remote_cb_payload payload = { Qnil, Qnil, Qnil, Qnil, 0 };

	rb_scan_args(argc, argv, "01:", &rb_refspecs, &rb_options);
	parse_fetch_options(&callbacks, rb_options, &payload);

	if (!NIL_P(rb_refspecs)) {
		Check_Type(rb_refspecs, T_ARRAY);
		for (i = 0; i < RARRAY_LEN(rb_refspecs); ++i) {
			Check_Type(rb_ary_entry(rb_refspecs, i), T_STRING);
		}
	}

	Data_Get_Struct(self, git_remote, remote);

	// Create a temporary remote that we use for fetching
	error = git_remote_create_inmemory(&tmp_remote, git_remote_owner(remote), NULL, git_remote_url(remote));
	rugged_exception_check(error);

	git_remote_set_autotag(tmp_remote, git_remote_autotag(remote));

	error = git_remote_set_callbacks(tmp_remote, &callbacks);
	if (error) goto cleanup;

	if (!NIL_P(rb_refspecs)) {
		for (i = 0; !error && i < RARRAY_LEN(rb_refspecs); ++i) {
			VALUE rb_refspec = rb_ary_entry(rb_refspecs, i);
			error = git_remote_add_fetch(tmp_remote, StringValueCStr(rb_refspec));
		}

		if (error) goto cleanup;
	} else {
		git_strarray refspecs;
		error = git_remote_get_fetch_refspecs(&refspecs, remote);

		if (!error) {
			error = git_remote_set_fetch_refspecs(tmp_remote, &refspecs);
		}

		git_strarray_free(&refspecs);

		if (error) goto cleanup;
	}

	error = git_remote_fetch(tmp_remote, NULL, NULL);

cleanup:

	git_remote_free(tmp_remote);

	if (RTEST(payload.exception))
		rb_jump_tag(payload.exception);

	rugged_exception_check(error);

	return Qnil;
}

void Init_rugged_remote(void)
{
	rb_cRuggedRemote = rb_define_class_under(rb_mRugged, "Remote", rb_cObject);

	rb_define_singleton_method(rb_cRuggedRemote, "new", rb_git_remote_new, 2);
	rb_define_singleton_method(rb_cRuggedRemote, "add", rb_git_remote_add, 3);
	rb_define_singleton_method(rb_cRuggedRemote, "lookup", rb_git_remote_lookup, 2);
	rb_define_singleton_method(rb_cRuggedRemote, "names", rb_git_remote_names, 1);
	rb_define_singleton_method(rb_cRuggedRemote, "each", rb_git_remote_each, 1);

	rb_define_method(rb_cRuggedRemote, "name", rb_git_remote_name, 0);
	rb_define_method(rb_cRuggedRemote, "url", rb_git_remote_url, 0);
	rb_define_method(rb_cRuggedRemote, "url=", rb_git_remote_set_url, 1);
	rb_define_method(rb_cRuggedRemote, "push_url", rb_git_remote_push_url, 0);
	rb_define_method(rb_cRuggedRemote, "push_url=", rb_git_remote_set_push_url, 1);
	rb_define_method(rb_cRuggedRemote, "fetch_refspecs", rb_git_remote_fetch_refspecs, 0);
	rb_define_method(rb_cRuggedRemote, "push_refspecs", rb_git_remote_push_refspecs, 0);
	rb_define_method(rb_cRuggedRemote, "add_fetch", rb_git_remote_add_fetch, 1);
	rb_define_method(rb_cRuggedRemote, "add_push", rb_git_remote_add_push, 1);
	rb_define_method(rb_cRuggedRemote, "ls", rb_git_remote_ls, 0);
	rb_define_method(rb_cRuggedRemote, "clear_refspecs", rb_git_remote_clear_refspecs, 0);
	rb_define_method(rb_cRuggedRemote, "save", rb_git_remote_save, 0);
	rb_define_method(rb_cRuggedRemote, "rename!", rb_git_remote_rename, 1);

	rb_define_method(rb_cRuggedRemote, "fetch", rb_git_remote_fetch, -1);
}
