// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: Listener Threads

internal void
w32_sock_listener_thread_entry_point(void *p)
{
  ThreadNameF("w32_sock_listener_thread_%I64u", (U64)p);
  SOCK_Protocol protocol = (SOCK_Protocol)p;
  for(;;)
  {
    //- rjf: 
    
    //- rjf: accept new connection on the TCP listener
    struct sockaddr_storage addr = {0};
    int addr_size = sizeof(addr);
    SOCKET new_socket = accept(w32_sock_state->tcp_listen_socket, (struct sockaddr *)&addr, &addr_size);
    
    //- rjf: got new TCP connection socket -> store connection
    if(new_socket != INVALID_SOCKET)
    {
      // rjf: unpack socket's endpoint info
      SOCK_Endpoint endpoint = {0};
      {
        switch(addr.ss_family)
        {
          default:{}break;
          case AF_INET:
          {
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)&addr;
            endpoint.port = ntohs(ipv4->sin_port);
            endpoint.address_u32[0] = ipv4->sin_addr.s_addr;
          }break;
          case AF_INET6:
          {
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)&addr;
            endpoint.port = ntohs(ipv6->sin6_port);
            MemoryCopy(endpoint.address_u8, ipv6->sin6_addr.u.Byte, sizeof(endpoint.address_u8));
          }break;
        }
      }
      
      // rjf: unpack endpoint
      U64 hash = u64_hash_from_str8(str8_struct(&endpoint));
      U64 slot_idx = hash%w32_sock_state->connection_slots_count;
      W32_SOCK_ConnectionSlot *slot = &w32_sock_state->connection_slots[slot_idx];
      Stripe *stripe = stripe_from_slot_idx(&w32_sock_state->connection_stripes, slot_idx);
      
      // rjf: store new connection
      RWMutexScope(stripe->rw_mutex, 1)
      {
        W32_SOCK_Connection *con = (W32_SOCK_Connection *)stripe->free;
        if(con != 0)
        {
          stripe->free = con->next;
        }
        else
        {
          con = push_array(stripe->arena, W32_SOCK_Connection, 1);
        }
        con->endpoint = endpoint;
        con->protocol = SOCK_Protocol_TCP;
        con->socket = new_socket;
        DLLPushBack(slot->first, slot->last, con);
      }
    }
  }
}

////////////////////////////////
//~ rjf: @per_os_impl Top-Level Layer Calls

internal void
sock_init(void)
{
  // NOTE(rjf): winsock2 is already initialized by the base layer for RIO function grabbing.
  
  //- rjf: set up state
  Arena *arena = arena_alloc();
  w32_sock_state = push_array(arena, W32_SOCK_State, 1);
  w32_sock_state->arena = arena;
  w32_sock_state->u2s_ring = guarded_ring_alloc(arena, KB(256));
  w32_sock_state->s2u_ring = guarded_ring_alloc(arena, KB(256));
  
  //- rjf: create listener sockets
  w32_sock_state->tcp_listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  w32_sock_state->udp_listen_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  {
    DWORD ipv6only = 0;
    setsockopt(w32_sock_state->tcp_listen_socket, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&ipv6only, sizeof(ipv6only));
    setsockopt(w32_sock_state->udp_listen_socket, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&ipv6only, sizeof(ipv6only));
  }
  
  //- rjf: bind listener sockets
  {
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SOCKET_PORT);
    bind(w32_sock_state->tcp_listen_socket, (SOCKADDR *)&server_addr, sizeof(server_addr));
    bind(w32_sock_state->udp_listen_socket, (SOCKADDR *)&server_addr, sizeof(server_addr));
  }
  
  //- rjf: start listening for TCP connections
  {
    listen(w32_sock_state->tcp_listen_socket, SOMAXCONN);
  }
  
  //- rjf: set up connection cache
  w32_sock_state->connection_slots_count = 8;
  w32_sock_state->connection_slots = push_array(arena, W32_SOCK_ConnectionSlot, w32_sock_state->connection_slots_count);
  w32_sock_state->connection_stripes = stripe_array_alloc(arena);
  
  //- rjf: launch one-off listener threads to block & accept connections
  w32_sock_state->tcp_listener_thread = thread_launch(w32_sock_listener_thread_entry_point, (void *)SOCK_Protocol_TCP);
}

internal void
sock_async_tick(void)
{
  Temp scratch = scratch_begin(0, 0);
  
  //////////////////////////////
  //- rjf: gather send tasks
  //
  typedef struct SendTask SendTask;
  struct SendTask
  {
    SendTask *next;
    SOCK_Endpoint endpoint;
    String8 data;
  };
  SendTask *first_tcp_send = 0;
  SendTask *last_tcp_send = 0;
  SendTask *first_udp_send = 0;
  SendTask *last_udp_send = 0;
  if(lane_idx() == 0)
  {
    for(;;)
    {
      B32 got_more = 0;
      RingGuard g = guarded_ring_open(w32_sock_state->u2s_ring);
      {
        U64 header[5] = {0};
        if(guarded_ring_try_read(&g, sizeof(header), header))
        {
          got_more = 1;
          SOCK_Protocol protocol = (SOCK_Protocol)header[0];
          U16 port = (U16)header[1];
          SOCK_Endpoint endpoint = {0};
          endpoint.address_u64[0] = header[2];
          endpoint.address_u64[1] = header[3];
          U64 data_size = header[4];
          U8 *data = push_array(scratch.arena, U8, data_size);
          guarded_ring_read_or_wait(&g, data_size, data, max_U64);
          SendTask *t = push_array(scratch.arena, SendTask, 1);
          t->endpoint = endpoint;
          t->data = str8(data, data_size);
          switch(protocol)
          {
            default:{}break;
            case SOCK_Protocol_TCP:{SLLQueuePush(first_tcp_send, last_tcp_send, t);}break;
            case SOCK_Protocol_UDP:{SLLQueuePush(first_tcp_send, last_tcp_send, t);}break;
          }
        }
      }
      guarded_ring_close(&g);
      if(!got_more)
      {
        break;
      }
    }
  }
  lane_sync();
  
  //////////////////////////////
  //- rjf: do TCP sends
  //
  for(SendTask *t = first_tcp_send; t != 0; t = t->next)
  {
    U64 hash = u64_hash_from_str8(str8_struct(&t->endpoint));
    U64 slot_idx = hash%w32_sock_state->connection_slots_count;
    W32_SOCK_ConnectionSlot *slot = &w32_sock_state->connection_slots[slot_idx];
    Stripe *stripe = stripe_from_slot_idx(&w32_sock_state->connection_stripes, slot_idx);
    RWMutexScope(stripe->rw_mutex, 1)
    {
      for(W32_SOCK_Connection *c = slot->first; c != 0; c = c->next)
      {
        if(MemoryMatchStruct(&c->endpoint, &t->endpoint))
        {
          if(send(c->socket, t->data.str, t->data.size, 0) == SOCKET_ERROR)
          {
            int error = WSAGetLastError();
            if(error == WSAECONNRESET)
            {
              DLLRemove(slot->first, slot->last, c);
              c->next = stripe->free;
              stripe->free = c;
            }
          }
        }
      }
    }
  }
  
  scratch_end(scratch);
}

////////////////////////////////
//~ rjf: @per_os_impl Sends

internal B32
sock_send(SOCK_Protocol protocol, SOCK_Endpoint endpoint, String8 data, U64 endt_us)
{
  B32 result = 0;
  RingGuard guard = guarded_ring_open(w32_sock_state->u2s_ring);
  {
    U64 header[5] = {0};
    U64 size_cap = w32_sock_state->u2s_ring->ring->size - sizeof(header);
    U64 size = Min(data.size, size_cap);
    {
      header[0] = (U64)protocol;
      header[1] = (U64)endpoint.port;
      header[2] = endpoint.address_u64[0];
      header[3] = endpoint.address_u64[1];
      header[4] = size;
    }
    if(guarded_ring_write_or_wait(&guard, sizeof(header), header, endt_us))
    {
      guarded_ring_write_or_wait(&guard, size, data.str, max_U64);
      result = 1;
    }
  }
  guarded_ring_close(&guard);
  if(result)
  {
    ins_atomic_u32_eval_assign(&async_loop_again, 1);
    cond_var_broadcast(async_tick_start_cond_var);
  }
  return result;
}

////////////////////////////////
//~ rjf: @per_os_impl Receives

internal B32
sock_recv(Arena *arena, SOCK_Protocol *protocol_out, SOCK_Endpoint *endpoint_out, String8 *data_out, U64 endt_us)
{
  B32 result = 0;
  U64 header_size = sizeof(*protocol_out) + sizeof(*endpoint_out) + sizeof(U64);
  {
    RingGuard guard = guarded_ring_open(w32_sock_state->s2u_ring);
    {
      U64 header[5] = {0};
      if(guarded_ring_read_or_wait(&guard, sizeof(header), header, endt_us))
      {
        U64 size_cap = w32_sock_state->s2u_ring->ring->size - sizeof(header);
        protocol_out[0] = (SOCK_Protocol)header[0];
        endpoint_out->port = (U16)header[1];
        endpoint_out->address_u64[0] = header[2];
        endpoint_out->address_u64[1] = header[3];
        data_out->size = Min(size_cap, header[4]);
        data_out->str = push_array(arena, U8, data_out->size);
        guarded_ring_read_or_wait(&guard, data_out->size, data_out->str, max_U64);
        result = 1;
      }
    }
    guarded_ring_close(&guard);
  }
  return result;
}
