#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "jabber.h"
#include "ggtrans.h"
#include "users.h"
#include "jid.h"
#include <glib.h>

GHashTable *users_uin=NULL;
GHashTable *users_jid=NULL;
static char *spool_dir;

int users_init(){
xmlnode node;
int r;

	node=xmlnode_get_tag(config,"spool");
	if (node) spool_dir=xmlnode_get_data(node);
	if (!spool_dir){
		fprintf(stderr,"No <spool/> defined in config file\n");
		return -1;
	}

	r=chdir(spool_dir);
	if (r){
		fprintf(stderr,"Couldn't enter %s\n",spool_dir);
		perror("chdir");
		return -1;
	}
	
	users_jid=g_hash_table_new(g_str_hash,g_str_equal);
	if (!users_jid) return -1;
	users_uin=g_hash_table_new(g_int_hash,g_int_equal);
	if (!users_uin) return -1;
	return 0;
}

static int user_destroy(User *s);

static gboolean users_hash_func(gpointer key,gpointer value,gpointer udata){

	user_destroy((User *)value);
	g_free(key);
	return TRUE;
}

int users_done(){

	g_hash_table_foreach_remove(users_jid,users_hash_func,NULL);
	g_hash_table_destroy(users_jid);
	g_hash_table_destroy(users_uin);
	return 0;
}

int user_save(User *u){
FILE *f;
char *fn;
char *str;
char *njid;
int r;	
xmlnode xml,tag,userlist;

	assert(u!=NULL);
	str=strchr(u->jid,'/');
	assert(str==NULL);

	fprintf(stderr,"\nSaving user '%s'\n",u->jid);
	njid=jid_normalized(u->jid);
	fn=g_strdup_printf("%s.new",njid);
	f=fopen(fn,"w");
	if (!f){
		fprintf(stderr,"Couldn't open '%s'\n",fn);
		g_free(fn);
		g_free(njid);
		perror("fopen");
		return -1;
	}
	xml=xmlnode_new_tag("user");
	tag=xmlnode_insert_tag(xml,"jid");
	xmlnode_insert_cdata(tag,u->jid,-1);
	tag=xmlnode_insert_tag(xml,"uin");
	str=g_strdup_printf("%lu",(unsigned long)u->uin);
	xmlnode_insert_cdata(tag,str,-1);
	g_free(str);
	tag=xmlnode_insert_tag(xml,"password");
	xmlnode_insert_cdata(tag,u->password,-1);
	if (u->email){
		tag=xmlnode_insert_tag(xml,"email");
		xmlnode_insert_cdata(tag,u->email,-1);
	}
	if (u->name){
		tag=xmlnode_insert_tag(xml,"name");
		xmlnode_insert_cdata(tag,u->name,-1);
	}

	if (u->contacts){
		GList *it;
		Contact *c;
		
		userlist=xmlnode_insert_tag(xml,"userlist");
		for(it=g_list_first(u->contacts);it;it=it->next){
			c=(Contact *)it->data;
			tag=xmlnode_insert_tag(userlist,"uin");
			str=g_strdup_printf("%lu",(unsigned long)c->uin);
			xmlnode_insert_cdata(tag,str,-1);
			g_free(str);
		}
	}

	str=xmlnode2str(xml);
	r=fputs(str,f);
	if (r<0){
		fprintf(stderr,"Couldn't save '%s'\n",u->jid);
		perror("fputs");
		fclose(f);
		unlink(fn);
		xmlnode_free(xml);
		g_free(fn);
		g_free(njid);
		return -1;
	}
	fclose(f);
	r=unlink(njid);
	if (r && errno!=ENOENT){
		fprintf(stderr,"Couldn't unlink '%s'\n",u->jid);
		perror("unlink");
		xmlnode_free(xml);
		g_free(fn);
		g_free(njid);
		return -1;
	}
	
	r=rename(fn,njid);
	if (r){
		fprintf(stderr,"Couldn't rename '%s' to '%s'\n",fn,u->jid);
		perror("rename");
		xmlnode_free(xml);
		g_free(fn);
		g_free(njid);
		return -1;
	}
	
	xmlnode_free(xml);
	g_free(fn);
	g_free(njid);
	return 0;
}

User *user_load(const char *jid){
char *fn,*njid;
xmlnode xml,tag,t;
char *uin,*ujid,*name,*password,*email;
User *u;
GList *contacts;
char *p;

	uin=ujid=name=password=email=NULL;
	fprintf(stderr,"\nLoading user '%s'\n",jid);
	fn=jid_normalized(jid);
	errno=0;
	xml=xmlnode_file(fn);
	if (!xml){
		fprintf(stderr,"Couldn't read or parse '%s'\n",fn);
		if (errno) perror("xmlnode_file");
		g_free(fn);
		return NULL;
	}
	g_free(fn);
	tag=xmlnode_get_tag(xml,"jid");
	if (tag) ujid=xmlnode_get_data(tag);
	if (!ujid){
		fprintf(stderr,"Couldn't find JID in users file\n");
		return NULL;
	}
	tag=xmlnode_get_tag(xml,"uin");
	if (tag) uin=xmlnode_get_data(tag);
	if (!uin){
		fprintf(stderr,"Couldn't find UIN in users file\n");
		return NULL;
	}
	tag=xmlnode_get_tag(xml,"password");
	if (tag) password=xmlnode_get_data(tag);
	if (!password){
		fprintf(stderr,"Couldn't find password in users file\n");
		return NULL;
	}
	tag=xmlnode_get_tag(xml,"email");
	if (tag) email=xmlnode_get_data(tag);
	tag=xmlnode_get_tag(xml,"name");
	if (tag) name=xmlnode_get_data(tag);
	tag=xmlnode_get_tag(xml,"userlist");
	contacts=NULL;
	if (tag){
		Contact *c;
		
		for(t=xmlnode_get_firstchild(tag);t;t=xmlnode_get_nextsibling(t))
			if (!g_strcasecmp(xmlnode_get_name(t),"uin")
					&& xmlnode_get_data(t)
					&& atoi(xmlnode_get_data(t)) ) {

					c=(Contact *)g_malloc(sizeof(Contact));
					memset(c,0,sizeof(*c));
					c->uin=atoi(xmlnode_get_data(t));	
					contacts=g_list_append(contacts,c);
			}
	}
	u=(User *)g_malloc(sizeof(User));
	memset(u,0,sizeof(User));
	u->uin=atoi(uin);
	u->jid=g_strdup(jid);
	p=strchr(u->jid,'/');
	if (p) *p=0;
	u->password=g_strdup(password);
	if (name) u->name=g_strdup(name);
	if (email) u->email=g_strdup(email);
	u->contacts=contacts;
	xmlnode_free(xml);
	assert(users_jid!=NULL);
	assert(users_uin!=NULL);
	njid=jid_normalized(u->jid);
	g_hash_table_insert(users_jid,(gpointer)njid,(gpointer)u);
	g_hash_table_insert(users_uin,GINT_TO_POINTER(u->uin),(gpointer)u);
	u->confirmed=1;
	return u;
}

User *user_get_by_uin(uin_t uin){
User *u;
char *njid;
	
	assert(users_uin!=NULL);
	njid=jid_normalized(u->jid);
	u=(User *)g_hash_table_lookup(users_uin,GINT_TO_POINTER(uin));
	g_free(njid);
	return u;
}

User *user_get_by_jid(const char *jid){
User *u;
char *str,*p;
	
	str=g_strdup(jid);
	p=strchr(str,'/');
	if (p) *p=0;
	assert(users_jid!=NULL);
	u=(User *)g_hash_table_lookup(users_jid,(gpointer)str);
	g_free(str);
	if (u) return u;
	return user_load(jid);	
}

static int user_destroy(User *u){

	fprintf(stderr,"\nRemoving user '%s'\n",u->jid);
	if (u->jid) g_free(u->jid);
	if (u->name) g_free(u->name);
	if (u->password) g_free(u->password);
	if (u->email) g_free(u->email);
	g_free(u);
	return 0;
}


int user_delete(User *u){

	if (u->uin && users_uin) g_hash_table_remove(users_uin,GINT_TO_POINTER(u->uin));
	if (users_jid){
		char *njid;
		gpointer key,value;
		njid=jid_normalized(u->jid);
		if (g_hash_table_lookup_extended(users_jid,(gpointer)u->jid,&key,&value)){
			g_hash_table_remove(users_jid,(gpointer)u->jid);
			g_free(key);
		}
		g_free(njid);
	}
	return user_destroy(u);
}

User *user_add(const char *jid,uin_t uin,const char *name,const char * password,const char *email){
User *u;
char *p,*njid;

	fprintf(stderr,"\nCreating user '%s'\n",jid);
	if (uin<1){
		fprintf(stderr,"Bad UIN\n");
		return NULL;
	}
	if (!password){
		fprintf(stderr,"Password not given\n");
		return NULL;
	}
	if (!jid){
		fprintf(stderr,"JID not given\n");
		return NULL;
	}

	u=(User *)g_malloc(sizeof(User));
	memset(u,0,sizeof(User));
	u->uin=uin;
	u->jid=g_strdup(jid);
	p=strchr(u->jid,'/');
	if (p) *p=0;
	u->password=g_strdup(password);
	if (name) u->name=g_strdup(name);
	else name=NULL;
	if (email) u->email=g_strdup(email);
	else name=NULL;
	u->confirmed=0;
	assert(users_jid!=NULL);
	assert(users_uin!=NULL);
	njid=jid_normalized(u->jid);
	g_hash_table_insert(users_jid,(gpointer)njid,(gpointer)u);
	g_hash_table_insert(users_uin,GINT_TO_POINTER(u->uin),(gpointer)u);
	return u;
}

int user_subscribe(User *u,uin_t uin){
Contact *c;	
GList *it;

	assert(u!=NULL);
	for(it=g_list_first(u->contacts);it;it=it->next){
		c=(Contact *)it->data;
		if (c->uin==uin) return -1;
	}

	c=(Contact *)g_malloc(sizeof(Contact));
	memset(c,0,sizeof(*c));

	c->uin=uin;

	u->contacts=g_list_append(u->contacts,c);
	if (u->confirmed) user_save(u);
	return 0;	
}

int user_unsubscribe(User *u,uin_t uin){
Contact *c;
GList *it;

	for(it=g_list_first(u->contacts);it;it=it->next){
		c=(Contact *)it->data;
		if (c->uin==uin){
			u->contacts=g_list_remove(u->contacts,c);
			g_free(c);
			if (u->confirmed) user_save(u);
			return 0;
		}
	}
	return -1;
}

int user_set_contact_status(User *u,int status,unsigned int uin){
GList *it;
Contact *c;

	assert(u!=NULL);
	for(it=u->contacts;it;it=it->next){
		c=(Contact *)it->data;
		if (c->uin==uin){
			c->status=status;
			return 0;
		}
	}
		
	return -1;	
}
