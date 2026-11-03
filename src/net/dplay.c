/* DirectPlay 4 over a gasm-relay room (see dplay.h).

   The relay ([1 WELCOME][index][peers], [2 JOIN][index], [3 LEAVE][index], [4 FULL], [0x10 DATA][from]
   [payload]; a sent DATA goes to every other peer) carries these payloads, byte 0 the operation:

     1 HERE                                 a peer came in (a host answers with SESSION)
     2 SESSION  [to][session]               the host's session (to 0xff: everyone)
     3 JOIN     [password]                  a peer asks the host in
     4 ACCEPT   [to][session]               the host lets it in (its players follow as ROSTER)
     5 REFUSE   [to][result u32]            or not (DPERR_INVALIDPASSWORD, a full session)
     6 PLAYER   [id][names][data]           a player created by the sender, every member told
     7 ROSTER   [to][owner][id][names][data] a player the joiner finds there (no system message)
     8 GONE     [id]                        a player destroyed
     9 PDATA    [id][data]                  a player's data set
    10 MSG      [from][to][data]            a message (to 0: every other player)
    11 LEAVE                                the sender left the session (the connection stays)

   with u32s little-endian, strings and data a u16 length then the bytes. A session is the room's;
   its members are the host and those it accepted; the host is the member with the lowest relay
   index once the first host has gone. Ids are 0x10000 + relay index * 0x100 + n. */
#include "net/dplay.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PEERS = 256, MAXMSG = 0x10000 };
enum { OP_HERE = 1, OP_SESSION, OP_JOIN, OP_ACCEPT, OP_REFUSE, OP_PLAYER, OP_ROSTER, OP_GONE, OP_PDATA, OP_MSG, OP_LEAVE };

typedef struct Msg { struct Msg *next; uint32_t from, to, len; uint8_t b[]; } Msg;
typedef struct { DpPlayer p; int owner; bool used; } Slot;

typedef struct {
    bool created, initialized;
    char url[256];
    int conn, index;            /* the connection, our relay index (-1 until WELCOME) */
    bool member, host, joining;
    int host_index;             /* the session's host's relay index (-1 none known) */
    bool in_room[PEERS], is_member[PEERS];
    DpSession session;
    DpSession found[DP_SESSIONS];
    int nfound;
    Slot players[DP_PLAYERS];
    int next_id;
    int join_state;             /* 0 not joining, 1 asked, 2 answered (join_result) */
    uint32_t join_result;
    Msg *head, *tail;
} DpState;

static DpState S[2];
static DpState *D = &S[0];

/* the tests: a second DirectPlay in the same process (a session's two ends over the native relay) */
void Dp_Select(int k) { D = &S[k & 1]; }

static uint8_t pkt[MAXMSG];

static void w32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }
static uint32_t r32(const uint8_t *p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* a payload being built after [0x10] */
static uint32_t at;
static void put8(uint8_t v) { if (at < MAXMSG) pkt[at++] = v; }
static void put32(uint32_t v) { if (at + 4 <= MAXMSG) w32(pkt + at, v), at += 4; }
static void putb(const void *b, uint32_t n)
{
    if (n > 0xffff || at + 2 + n > MAXMSG) n = 0;
    pkt[at++] = (uint8_t)n, pkt[at++] = (uint8_t)(n >> 8);
    memcpy(pkt + at, b, n), at += n;
}
static void puts_(const char *s) { putb(s, (uint32_t)strlen(s)); }
static void begin(int op) { at = 0, put8(0x10), put8((uint8_t)op); }
static void flush(void) { if (D->conn > 0) plat_net_send(D->conn, pkt, at); }

/* reading one */
static const uint8_t *rp, *re;
static bool bad;
static uint8_t get8(void) { if (rp + 1 > re) return bad = true, 0; return *rp++; }
static uint32_t get32(void) { if (rp + 4 > re) return bad = true, 0; uint32_t v = r32(rp); rp += 4; return v; }
static uint32_t getb(void *dst, uint32_t cap)
{
    if (rp + 2 > re) return bad = true, 0;
    uint32_t n = (uint32_t)rp[0] | rp[1] << 8;
    rp += 2;
    if (rp + n > re) return bad = true, 0;
    uint32_t k = n < cap ? n : cap;
    memcpy(dst, rp, k), rp += n;
    return k;
}
static void gets_(char *dst, uint32_t cap)
{
    uint32_t n = getb(dst, cap - 1);
    dst[n] = 0;
}

static void put_session(const DpSession *s)
{
    put32(s->flags);
    putb(s->instance, 16);
    put32(s->max_players), put32(s->cur_players);
    puts_(s->name);
    put8(s->password[0] != 0);   /* whether there is one, not the password */
    for (int i = 0; i < 4; i++) put32(s->user[i]);
}
static void get_session(DpSession *s)
{
    memset(s, 0, sizeof *s);
    s->flags = get32();
    getb(s->instance, 16);
    s->max_players = get32(), s->cur_players = get32();
    gets_(s->name, sizeof s->name);
    s->flags &= ~DPSESSION_PASSWORDREQUIRED;
    if (get8()) s->flags |= DPSESSION_PASSWORDREQUIRED;   /* DirectPlay's own mark: the host has a password */
    for (int i = 0; i < 4; i++) s->user[i] = get32();
}

static void put_player(const DpPlayer *p)
{
    put32(p->id);
    puts_(p->short_name), puts_(p->long_name);
    putb(p->data, p->data_len);
}

static void queue(uint32_t from, uint32_t to, const void *b, uint32_t len)
{
    Msg *m = malloc(sizeof *m + len);
    if (!m) return;
    m->next = NULL, m->from = from, m->to = to, m->len = len;
    memcpy(m->b, b, len);
    if (D->tail) D->tail->next = m;
    else D->head = m;
    D->tail = m;
}

static void sysmsg(uint32_t type, const DpPlayer *p)
{
    DpSysMsg s;
    memset(&s, 0, sizeof s);
    s.type = type;
    if (p) {
        s.player = p->id;
        memcpy(s.short_name, p->short_name, sizeof s.short_name);
        memcpy(s.long_name, p->long_name, sizeof s.long_name);
        memcpy(s.data, p->data, p->data_len), s.data_len = p->data_len;
    }
    s.cur_players = D->session.cur_players;
    if (type == DPSYS_SETSESSIONDESC) s.session = D->session;
    queue(DPID_SYSMSG, 0, &s, sizeof s);
}

static Slot *find(uint32_t id)
{
    for (int i = 0; i < DP_PLAYERS; i++)
        if (D->players[i].used && D->players[i].p.id == id) return &D->players[i];
    return NULL;
}

static Slot *add(int owner, const DpPlayer *p)
{
    Slot *s = find(p->id);
    for (int i = 0; !s && i < DP_PLAYERS; i++)
        if (!D->players[i].used) s = &D->players[i];
    if (!s) return NULL;
    s->used = true, s->owner = owner, s->p = *p;
    s->p.local = owner == D->index;
    return s;
}

static int count_players(void)
{
    int n = 0;
    for (int i = 0; i < DP_PLAYERS; i++) n += D->players[i].used;
    return n;
}

/* a member gone (left or disconnected): its players destroyed, and a new host if it was the host */
static void member_gone(int idx)
{
    if (idx < 0 || idx >= PEERS || !D->is_member[idx]) return;
    D->is_member[idx] = false;
    for (int i = 0; i < DP_PLAYERS; i++)
        if (D->players[i].used && D->players[i].owner == idx) {
            if (D->member) sysmsg(DPSYS_DESTROYPLAYERORGROUP, &D->players[i].p);
            D->players[i].used = false;
        }
    D->session.cur_players = (uint32_t)count_players();
    if (idx != D->host_index || !D->member) return;
    int h = -1;
    for (int k = 0; k < PEERS && h < 0; k++)
        if (D->is_member[k]) h = k;
    D->host_index = h;
    if (h == D->index && !(D->session.flags & 0x4)) {   /* without DPSESSION_MIGRATEHOST the session is lost */
        sysmsg(DPSYS_SESSIONLOST, NULL);
        D->member = false;
        return;
    }
    if (h == D->index) {
        D->host = true;
        for (int i = 0; i < DP_PLAYERS; i++)
            if (D->players[i].used && D->players[i].owner == D->index) {
                DpSysMsg m;
                memset(&m, 0, sizeof m);
                m.type = DPSYS_HOST, m.player = D->players[i].p.id;
                queue(DPID_SYSMSG, 0, &m, sizeof m);
                break;
            }
        begin(OP_SESSION), put8(0xff), put_session(&D->session), flush();
    }
}

static void found_session(const DpSession *s)
{
    for (int i = 0; i < D->nfound; i++)
        if (!memcmp(D->found[i].instance, s->instance, 16)) {
            D->found[i] = *s;
            return;
        }
    if (D->nfound < DP_SESSIONS) D->found[D->nfound++] = *s;
}

static void handle(int from, const uint8_t *b, uint32_t len)
{
    rp = b, re = b + len, bad = false;
    int op = get8();
    switch (op) {
    case OP_HERE:
        if (D->host) begin(OP_SESSION), put8((uint8_t)from), put_session(&D->session), flush();
        break;
    case OP_SESSION: {
        int to = get8();
        DpSession s;
        get_session(&s);
        if (bad || (to != 0xff && to != D->index)) break;
        if (!D->member) found_session(&s);
        else if (from == D->host_index || D->host_index < 0) {
            D->host_index = from;
            char keep[DP_NAME];
            memcpy(keep, D->session.password, sizeof keep);
            D->session = s;
            memcpy(D->session.password, keep, sizeof keep);
            sysmsg(DPSYS_SETSESSIONDESC, NULL);
        }
        break;
    }
    case OP_JOIN: {
        char pw[DP_NAME];
        gets_(pw, sizeof pw);
        if (bad || !D->host) break;
        uint32_t r = DP_OK;
        if (D->session.password[0] && strcmp(pw, D->session.password)) r = DPERR_INVALIDPASSWORD;
        else if (D->session.flags & DPSESSION_JOINDISABLED) r = DPERR_NONEWPLAYERS;
        else if (D->session.max_players && (uint32_t)count_players() >= D->session.max_players) r = DPERR_NONEWPLAYERS;
        if (r != DP_OK) {
            begin(OP_REFUSE), put8((uint8_t)from), put32(r), flush();
            break;
        }
        D->is_member[from] = true;
        begin(OP_ACCEPT), put8((uint8_t)from), put_session(&D->session), flush();
        for (int i = 0; i < DP_PLAYERS; i++)
            if (D->players[i].used) {
                begin(OP_ROSTER), put8((uint8_t)from), put8((uint8_t)D->players[i].owner), put_player(&D->players[i].p), flush();
            }
        break;
    }
    case OP_ACCEPT: {
        int to = get8();
        DpSession s;
        get_session(&s);
        if (bad || to != D->index || !D->joining) break;
        char keep[DP_NAME];
        memcpy(keep, D->session.password, sizeof keep);
        D->session = s;
        memcpy(D->session.password, keep, sizeof keep);
        D->member = true, D->joining = false, D->join_state = 2, D->join_result = DP_OK;
        D->host_index = from;
        D->is_member[from] = true, D->is_member[D->index] = true;
        break;
    }
    case OP_REFUSE: {
        int to = get8();
        uint32_t r = get32();
        if (!bad && to == D->index && D->joining) D->joining = false, D->join_state = 2, D->join_result = r;
        break;
    }
    case OP_PLAYER: case OP_ROSTER: {
        int owner = from;
        if (op == OP_ROSTER) {
            int to = get8();
            owner = get8();
            if (to != D->index) break;
        }
        DpPlayer p;
        memset(&p, 0, sizeof p);
        p.id = get32();
        gets_(p.short_name, sizeof p.short_name);
        gets_(p.long_name, sizeof p.long_name);
        p.data_len = getb(p.data, sizeof p.data);
        if (bad || !D->member) break;
        D->is_member[owner] = true;
        bool fresh = !find(p.id);
        if (!add(owner, &p)) break;
        D->session.cur_players = (uint32_t)count_players();
        if (op == OP_PLAYER && fresh) sysmsg(DPSYS_CREATEPLAYERORGROUP, &p);
        break;
    }
    case OP_GONE: {
        uint32_t id = get32();
        Slot *s = find(id);
        if (bad || !s || s->owner != from) break;
        if (D->member) sysmsg(DPSYS_DESTROYPLAYERORGROUP, &s->p);
        s->used = false;
        D->session.cur_players = (uint32_t)count_players();
        break;
    }
    case OP_PDATA: {
        uint32_t id = get32();
        Slot *s = find(id);
        uint8_t d[DP_DATA];
        uint32_t n = getb(d, sizeof d);
        if (bad || !s || s->owner != from) break;
        memcpy(s->p.data, d, n), s->p.data_len = n;
        if (D->member && !(D->session.flags & DPSESSION_NODATAMESSAGES)) sysmsg(DPSYS_SETPLAYERORGROUPDATA, &s->p);
        break;
    }
    case OP_MSG: {
        uint32_t f = get32(), to = get32();
        if (bad || !D->member || rp + 2 > re) break;
        uint32_t n = (uint32_t)rp[0] | rp[1] << 8;
        rp += 2;
        if (rp + n > re) break;
        Slot *t = to ? find(to) : NULL;
        if (to && (!t || !t->p.local)) break;
        if (!to) {
            /* to every player: one copy for each of ours */
            for (int i = 0; i < DP_PLAYERS; i++)
                if (D->players[i].used && D->players[i].p.local) queue(f, D->players[i].p.id, rp, n);
        } else queue(f, to, rp, n);
        break;
    }
    case OP_LEAVE:
        member_gone(from);
        break;
    }
}

static void pump(void)
{
    if (D->conn <= 0) return;
    static uint8_t in[MAXMSG + 8];
    for (;;) {
        int n = plat_net_recv(D->conn, in, sizeof in);
        if (n == 0) break;
        if (n < 0) {
            if (D->member) sysmsg(DPSYS_SESSIONLOST, NULL);
            plat_net_close(D->conn);
            D->conn = 0, D->member = D->host = D->joining = false;
            if (D->join_state == 1) D->join_state = 2, D->join_result = DPERR_NOCONNECTION;
            break;
        }
        if ((uint32_t)n > sizeof in) {   /* too big for us: dropped (the relay's own limit is lower) */
            static uint8_t *big;
            free(big);
            big = malloc((size_t)n);
            if (big) plat_net_recv(D->conn, big, (uint32_t)n);
            continue;
        }
        switch (in[0]) {
        case 1:   /* WELCOME */
            if (n >= 3) {
                D->index = in[1];
                memset(D->in_room, 0, sizeof D->in_room);
                D->in_room[D->index] = true;
                begin(OP_HERE), flush();
            }
            break;
        case 2:   /* JOIN */
            if (n >= 2) D->in_room[in[1]] = true;
            break;
        case 3:   /* LEAVE */
            if (n >= 2) D->in_room[in[1]] = false, member_gone(in[1]);
            break;
        case 4:   /* FULL */
            break;
        case 0x10:
            if (n >= 2) handle(in[1], in + 2, (uint32_t)n - 2);
            break;
        }
    }
}

/* connected to the room (opening it on the first call): DP_OK, DPERR_CONNECTING, or a failure */
static uint32_t connected(void)
{
    if (!D->initialized) return DPERR_UNINITIALIZED;
    if (D->conn <= 0) {
        D->conn = plat_net_open(D->url);
        D->index = -1;
        if (D->conn <= 0) return D->conn = 0, DPERR_NOCONNECTION;
    }
    int st = plat_net_state(D->conn);
    if (st == PLAT_NET_CONNECTING) return DPERR_CONNECTING;
    if (st != PLAT_NET_OPEN) {
        pump();
        return DPERR_NOCONNECTION;
    }
    pump();
    return D->index >= 0 ? DP_OK : DPERR_CONNECTING;
}

bool Dp_Create(void)
{
    memset(D, 0, sizeof *D);
    D->created = true, D->host_index = -1, D->index = -1;
    return true;
}

void Dp_Release(void)
{
    Dp_Close();
    if (D->conn > 0) plat_net_close(D->conn);
    for (Msg *m = D->head, *n; m; m = n) n = m->next, free(m);
    memset(D, 0, sizeof *D);
}

bool Dp_Created(void) { return D->created; }

int Dp_Connections(const char **names, int cap)
{
    if (cap > 0) names[0] = "Internet (relay)";
    return 1;
}

/* the room: relay=ws://host:port and room=<name> (default "opencmr2") */
uint32_t Dp_InitializeConnection(int i)
{
    if (!D->created || i != 0) return DPERR_INVALIDPARAMS;
    if (D->initialized) return DPERR_ALREADYINITIALIZED;
    char relay[200] = "", room[48] = "opencmr2";
    if (!plat_param("relay", relay, sizeof relay) || !relay[0]) return DPERR_NOCONNECTION;
    plat_param("room", room, sizeof room);
    size_t l = strlen(relay);
    while (l && relay[l - 1] == '/') relay[--l] = 0;
    snprintf(D->url, sizeof D->url, "%s/%s", relay, room);
    D->initialized = true;
    connected();   /* the room opened now, so a session can be hosted when the page asks */
    return DP_OK;
}

uint32_t Dp_EnumSessions(DpSession *out, int cap, int *n)
{
    *n = 0;
    uint32_t r = connected();
    if (r != DP_OK) return r;
    for (int i = 0; i < D->nfound && *n < cap; i++) out[(*n)++] = D->found[i];
    return DP_OK;
}

uint32_t Dp_Open(const DpSession *s, uint32_t how)
{
    if (D->join_state == 2 && !(how & DPOPEN_CREATE)) {   /* the join's answer, once */
        D->join_state = 0;
        return D->join_result;
    }
    if (D->member) return DPERR_ALREADYINITIALIZED;
    uint32_t r = connected();
    if (r != DP_OK) return r;
    if (how & DPOPEN_CREATE) {
        for (int k = 0; k < PEERS; k++)
            if (D->is_member[k]) return DPERR_ALREADYINITIALIZED;
        if (D->nfound) return DPERR_ALREADYINITIALIZED;   /* the room has a session already */
        D->session = *s;
        for (int i = 0; i < 16; i++) D->session.instance[i] = (uint8_t)(rand() >> 3);
        D->session.cur_players = 0;
        D->member = D->host = true;
        D->host_index = D->index;
        memset(D->is_member, 0, sizeof D->is_member);
        D->is_member[D->index] = true;
        begin(OP_SESSION), put8(0xff), put_session(&D->session), flush();
        return DP_OK;
    }
    /* joining: asked once, then DPERR_CONNECTING until the host answers (the original's Open waits);
       the answer once, then a new Open asks again */
    if (D->join_state == 2) {
        D->join_state = 0;
        return D->join_result;
    }
    if (!D->joining) {
        bool have = false;
        for (int i = 0; i < D->nfound; i++)
            if (!memcmp(D->found[i].instance, s->instance, 16)) have = true;
        if (!have) return DPERR_NOCONNECTION;   /* (DPERR_NOSESSIONS the original would take as joined) */
        memcpy(D->session.password, s->password, sizeof D->session.password);
        D->joining = true, D->join_state = 1;
        begin(OP_JOIN), puts_(s->password), flush();
        return DPERR_CONNECTING;
    }
    if (D->joining) return DPERR_CONNECTING;
    D->join_state = 0;
    return D->join_result;
}

uint32_t Dp_Close(void)
{
    if (!D->member && !D->joining) return DP_OK;
    for (int i = 0; i < DP_PLAYERS; i++)
        if (D->players[i].used && D->players[i].p.local) begin(OP_GONE), put32(D->players[i].p.id), flush();
    begin(OP_LEAVE), flush();
    memset(D->players, 0, sizeof D->players);
    memset(D->is_member, 0, sizeof D->is_member);
    D->member = D->host = D->joining = false;
    D->host_index = -1;
    D->nfound = 0;
    for (Msg *m = D->head, *n; m; m = n) n = m->next, free(m);
    D->head = D->tail = NULL;
    return DP_OK;
}

uint32_t Dp_GetSessionDesc(DpSession *s)
{
    if (!D->member) return DPERR_NOSESSIONS;
    pump();
    *s = D->session;
    s->cur_players = (uint32_t)count_players();
    return DP_OK;
}

uint32_t Dp_SetSessionDesc(const DpSession *s)
{
    if (!D->member) return DPERR_NOSESSIONS;
    if (!D->host) return DPERR_ACCESSDENIED;
    uint8_t inst[16];
    memcpy(inst, D->session.instance, 16);
    D->session = *s;
    memcpy(D->session.instance, inst, 16);
    begin(OP_SESSION), put8(0xff), put_session(&D->session), flush();
    return DP_OK;
}

uint32_t Dp_CreatePlayer(uint32_t *id, const char *short_name, const char *long_name, const void *data, uint32_t len)
{
    if (!D->member) return DPERR_NOSESSIONS;
    if (len > DP_DATA) return DPERR_INVALIDPARAMS;
    DpPlayer p;
    memset(&p, 0, sizeof p);
    p.id = 0x10000u + (uint32_t)D->index * 0x100u + (uint32_t)(++D->next_id & 0xff);
    snprintf(p.short_name, sizeof p.short_name, "%s", short_name ? short_name : "");
    snprintf(p.long_name, sizeof p.long_name, "%s", long_name ? long_name : "");
    if (len) memcpy(p.data, data, len);
    p.data_len = len;
    if (!add(D->index, &p)) return DPERR_GENERIC;
    D->session.cur_players = (uint32_t)count_players();
    begin(OP_PLAYER), put_player(&p), flush();
    *id = p.id;
    return DP_OK;
}

uint32_t Dp_DestroyPlayer(uint32_t id)
{
    Slot *s = find(id);
    if (!s || !s->p.local) return DPERR_INVALIDPLAYER;
    begin(OP_GONE), put32(id), flush();
    s->used = false;
    D->session.cur_players = (uint32_t)count_players();
    return DP_OK;
}

uint32_t Dp_SetPlayerData(uint32_t id, const void *data, uint32_t len)
{
    Slot *s = find(id);
    if (!s || !s->p.local) return DPERR_INVALIDPLAYER;
    if (len > DP_DATA) return DPERR_INVALIDPARAMS;
    memcpy(s->p.data, data, len), s->p.data_len = len;
    begin(OP_PDATA), put32(id), putb(data, len), flush();
    return DP_OK;
}

int Dp_Players(const DpPlayer **out, int cap)
{
    pump();
    int n = 0;
    /* the host's first, then by id */
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < DP_PLAYERS; i++) {
            if (!D->players[i].used || (D->players[i].owner == D->host_index) != (pass == 0)) continue;
            if (n < cap) out[n] = &D->players[i].p;
            n++;
        }
    return n;
}

uint32_t Dp_Send(uint32_t from, uint32_t to, bool guaranteed, const void *data, uint32_t len)
{
    (void)guaranteed;   /* the relay's are all guaranteed and in order */
    if (!D->member) return DPERR_NOSESSIONS;
    Slot *f = find(from);
    if (!f || !f->p.local) return DPERR_INVALIDPLAYER;
    if (to && !find(to)) return DPERR_INVALIDPLAYER;
    if (len > MAXMSG - 16) return DPERR_INVALIDPARAMS;
    begin(OP_MSG), put32(from), put32(to), putb(data, len), flush();
    /* to every player: the sender's other local players too (DirectPlay doesn't send to the sender) */
    if (!to) {
        for (int i = 0; i < DP_PLAYERS; i++)
            if (D->players[i].used && D->players[i].p.local && D->players[i].p.id != from) queue(from, D->players[i].p.id, data, len);
    } else if (find(to)->p.local) queue(from, to, data, len);
    return DP_OK;
}

uint32_t Dp_Receive(uint32_t *from, uint32_t *to, void *buf, uint32_t *len)
{
    pump();
    Msg *m = D->head;
    if (!m) return DPERR_NOMESSAGES;
    if (m->len > *len) {
        *len = m->len;
        return DPERR_BUFFERTOOSMALL;
    }
    *from = m->from, *to = m->to, *len = m->len;
    memcpy(buf, m->b, m->len);
    D->head = m->next;
    if (!D->head) D->tail = NULL;
    free(m);
    return DP_OK;
}

bool Dp_IsHost(void) { return D->member && D->host; }
