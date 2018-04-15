/****************************************************************************
 * dhtdigg.c                                                                *
 * Copyright (C) 2018 Gwiz <gwiz2009@gmail.com>                             *
 *                                                                          *
 * dhtdigg is free software: you can redistribute it and/or modify it       *
 * under the terms of the GNU General Public License as published by the    *
 * Free Software Foundation, either version 3 of the License, or            *
 * (at your option) any later version.                                      *
 *                                                                          *
 * dhtdigg is distributed in the hope that it will be useful, but           *
 * WITHOUT ANY WARRANTY; without even the implied warranty of               *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                     *
 * See the GNU General Public License for more details.                     *
 *                                                                          *
 * You should have received a copy of the GNU General Public License along  *
 * with this program.  If not, see <http://www.gnu.org/licenses/>.          *
 *                                                                          *
 ****************************************************************************/

#include "dhtdigg.h" 

/****************************
 *       Defines            *
 ****************************/
// use the local (not installed) ui file
//#define UI_FILE "src/dhtdigg.ui"

// use the installed ui file
#define UI_FILE PACKAGE_DATA_DIR"/ui/dhtdigg.ui"

#define MAX_PEERPROSPECTS 64
#define MSGBUFFERSIZE 65536

/****************************
 *       Globals            *
 ****************************/
int dhtpipe[2];
GtkTextBuffer *DHTtextbuffer;
GtkTextBuffer *BTtextbuffer;
GtkScrolledWindow *DHTWindowScrollView;
GtkScrolledWindow *BTWindowScrollView;
gboolean dhtloop = TRUE;
gboolean getmetadataloop = TRUE;
static unsigned char buf[4096];
int s = -1;
int s6 = -1;
struct peerlist PeerListQueue[MAX_PEERPROSPECTS];
int peerlistopenslot = 0;
int peerlistnum = 0;
struct bootstrap_storage Bootstrap;
static char btwritebuffer[256];
gchar *torrentdir;

/******************************************************************************
 *                                                                            *
 * Function: DisplayBTmsg                                                     *
 *                                                                            *
 * Purpose : called by WritetoBTwindow to insert BT message in a thread       * 
 *           safe manner                                                      *
 *                                                                            *
 ******************************************************************************/
gboolean DisplayBTmsg(void)
{
	GtkTextIter iter;
	GtkAdjustment *vadj;

	// insert into tex buffer
	gtk_text_buffer_get_end_iter (BTtextbuffer, &iter);
	gtk_text_buffer_insert (BTtextbuffer, &iter, btwritebuffer, -1);

	// scroll to bottom
	vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW
	                                           (BTWindowScrollView));
	gtk_adjustment_set_value (GTK_ADJUSTMENT(vadj), gtk_adjustment_get_upper 
	                          (GTK_ADJUSTMENT(vadj)));
	gtk_scrolled_window_set_vadjustment(GTK_SCROLLED_WINDOW
	                                    (BTWindowScrollView), 
	                                    GTK_ADJUSTMENT (vadj));
	return FALSE; // run once
}

/******************************************************************************
 *                                                                            *
 * Function: WritetoBTwindow                                                  *
 *                                                                            *
 * Purpose : writes to BTwindow                                               *
 *                                                                            *
 ******************************************************************************/
void WritetoBTwindow(char *format, ...)
{
	va_list args;

	// get args
	va_start(args, format);
	vsprintf(btwritebuffer, format, args);
	va_end(args);
	// call insert routine
	gdk_threads_add_idle((GSourceFunc) DisplayBTmsg, NULL);
	usleep(50000); 
}

/******************************************************************************
 *                                                                            *
 * Function: SendBTmsg                                                        *
 *                                                                            *
 * Purpose : returns bytes sent minus size header, 0=disconnect, -1=error     *
 *           payload points to message without the 4 byte message size header *
 *			 payload_size is number of bytes of payload                       * 
 *                                                                            *
 ******************************************************************************/
int SendBTmsg(int sockfd, char * payload, uint32_t payload_size)
{

	uint32_t bemsgsize = 0;
	int ret = 0;

	// send message size
	bemsgsize = htonl(payload_size);
	ret = send(sockfd, &bemsgsize, 4, 0);
	if (ret == 0) return 0;
	if (ret < 0) return -1;
	if (ret != 4) return -1;
	// send message
	ret = send(sockfd, payload, payload_size, 0);
	if (ret == 0) return 0;
	if (ret < 0) return -1;
	if (ret != payload_size) return -1;
	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: GetBTmsg                                                         *
 *                                                                            *
 * Purpose : returns size of payload, 0=disconnect, -1=errror, -2=strikeout   *
 *			 fills payload with recieved message                              *
 *			 btmsgwant indicates the bittorent message we are looking for     *
 *			 exmsgwant indicates the extended message we are looking for      *
 *                                                                            *
 *			 This function will read 3 messages looking for what we want      *
 *			 and will respond with keepalive to nonappicable messages         *				
 *                                                                            *
 ******************************************************************************/
int GetBTmsg(int sockfd, char * payload, int btmsgwant, int exmsgwant)
{
	int bGotWanted = FALSE;
	int tries = 0;
	int bytesread = 0;

	while (bGotWanted == FALSE)
	{
		char msg_len[4];
		int msgsize= 0;
		uint32_t tmp;

		if (tries > 2) return -2;
		// read length
		bytesread = 0;
		while (bytesread < 4)
		{
			int recvd = 0;
			recvd = recv(sockfd, &msg_len[bytesread], 4 - bytesread, 0);
			if (recvd == 0) return 0;
			if (recvd == -1) return -1;
			bytesread = bytesread + recvd;
		}
		if (bytesread != 4) return -1;
		memcpy(&tmp, &msg_len, 4);
		msgsize = ntohl(tmp);
		// read rest of msg
		bytesread = 0;
		while (bytesread < msgsize)
		{
			int recvd = 0;
			recvd = recv(sockfd, &payload[bytesread], msgsize - bytesread, 0);
			if (recvd == 0) return 0;
			if (recvd == -1) return -1;
			bytesread = bytesread + recvd;
		}
		if (bytesread != msgsize) return -1;
		if (payload[0] == btmsgwant && payload[1] == exmsgwant)
			bGotWanted = TRUE;
		else
		{
			if (payload[0] == 5 || payload[0] == 4)
			{
				// bitfield or have messages are balls, not strikes
				tries--;
			}
			else
			{
				// send keepalive
				WritetoBTwindow("Got BT message %i. Sending keepalive.\n", payload[0]);
				uint32_t keepalive = 0;
				send(sockfd, &keepalive, 4, 0);
			}
		}
		tries++;
	}
	return bytesread;
}

/******************************************************************************
 *                                                                            *
 * Function: TimedConnect                                                     *
 *                                                                            *
 * Purpose : socket connect with timeout                                      *
 *                                                                            *
 ******************************************************************************/
int TimedConnect(int sockfd, const struct sockaddr *addr, socklen_t addrlen, int secs)
{
	fd_set fdset;
	struct timeval tv;

	fcntl(sockfd, F_SETFL, O_NONBLOCK);
	connect(sockfd, addr, addrlen);
	FD_ZERO(&fdset);
	FD_SET(sockfd, &fdset);
	tv.tv_sec = secs;             
	tv.tv_usec = 0;
	if (select(sockfd + 1, NULL, &fdset, NULL, &tv) == 1)
	{
		int so_error;
		socklen_t len = sizeof(so_error);

		getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_error, &len);
		if (so_error == 0) 
		{
			fcntl(sockfd, F_SETFL, O_ASYNC);
			return 0;
		}
		else return -1;
	}
	else return -1;
}

/******************************************************************************
 *                                                                            *
 * Function: RestartDHT                                                       *
 *                                                                            *
 * Purpose :                                                                  *
 *                                                                            *
 ******************************************************************************/
gboolean RestartDHT (void)
{
	struct sockaddr_in sin[6];
	struct sockaddr_in6 sin6[6];
	int num = 6, num6 = 6;
	int i;

	fprintf(dht_debug, "\n******************************\n");
	fflush(dht_debug);
	fprintf(dht_debug, "Restarting DHT......\n");
	fflush(dht_debug);
	fprintf(dht_debug, "******************************\n");
	fflush(dht_debug);
	// collect some known nodes
	i = dht_get_nodes(sin, &num, sin6, &num6);
	fprintf(dht_debug, "Saving %d (%d + %d) as bootstrap nodes.\n", i, num, num6);
	fflush(dht_debug);
	if (num > 0)
	{
		int ctr = 0;
		while (ctr < num)
		{
			memcpy(&Bootstrap.IPv4bootnodes[ctr], &sin[ctr], sizeof(struct sockaddr_in));
			ctr++;
		}
		Bootstrap.numofIPv4s = ctr;
	}
	if (num6 > 0)
	{
		int ctr = 0;
		while (ctr < num6)
		{
			memcpy(&Bootstrap.IPv6bootnodes[ctr], &sin6[ctr], sizeof(struct sockaddr_in6));
			ctr++;
		}
		Bootstrap.numofIPv6s = ctr;
	}

	// stop thread
	dhtloop = FALSE;
	sleep(30);

	// reset stuff
	dhtloop = TRUE;
	s = -1;
	s6 = -1;
	// start dht thread
	g_thread_new ("dhtthread", (GThreadFunc) DhtThread, NULL);
	return TRUE; // keep running
}

/****************************************************************************
 *                                                                          *
 * Function: GetMetadataThread                                              *
 *                                                                          *
 * Purpose :                                                                *
 *                                                                          *
 ****************************************************************************/
gint GetMetadataThread (void) 
{
	while (getmetadataloop)
	{
		// see if we have something to connect to 
		if (!memcmp(&PeerListQueue[peerlistnum].info_hash, &zeros, 20) == 0)
		{
			int peer_sock_fd;
			struct sockaddr_in peer_sock_addr;

			WritetoBTwindow("Attempting peer connect from slot %i.\n", peerlistnum);
			// fill sockaddr
			peer_sock_addr.sin_family = AF_INET;
			memcpy(&(peer_sock_addr.sin_addr), &(PeerListQueue[peerlistnum].addr), 4);
			memcpy(&(peer_sock_addr.sin_port), &(PeerListQueue[peerlistnum].prt), 2);
			// create socket
			peer_sock_fd = socket(AF_INET, SOCK_STREAM, 0); 
			// connect with timeout
			if (!(TimedConnect(peer_sock_fd, (const struct sockaddr*) &peer_sock_addr, 
			                   sizeof(struct sockaddr_in), 12) < 0 ))
			{
				unsigned char handshake_msg[68];
				int hshake_sent;
				struct timeval timeout;      

				// set read & write timeouts
				timeout.tv_sec = 30;
				timeout.tv_usec = 0;
				setsockopt (peer_sock_fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
				setsockopt (peer_sock_fd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
				// send bt handshake
				memset(&handshake_msg[0], 0x0, 68);
				memset(&handshake_msg[0], 0x13, 1);
				strcpy((char *) &handshake_msg[1], "BitTorrent protocol");
				//(reserved_byte[5] & 0x10) Support Extended
				memset(&handshake_msg[25], 0x10, 1);
				memcpy(&handshake_msg[28], &PeerListQueue[peerlistnum].info_hash, 20);
				// give us a cool peer id
				strcpy((char *) &handshake_msg[48], "-DhtDigg");
				// random for rest of id
				dht_random_bytes(&handshake_msg[56], 12);
				// send handshake
				hshake_sent = send(peer_sock_fd, &handshake_msg, 68, 0);
				if (hshake_sent == 68)
				{
					unsigned char handshakebuffer[69]; 
					int hshake_recd;
					char temp[2048];

					memset(&handshakebuffer[0], 0x0, 68);
					WritetoBTwindow("Handshake message sent.\n");
					// receive return handshake
					hshake_recd = recv(peer_sock_fd, handshakebuffer, 68, 0);
					if (hshake_recd == 68)
					{
						strncpy(&temp[0], (char *) &handshakebuffer[48], 8);
						temp[8] = '\0';
						WritetoBTwindow("Received handshake. Client code: %s\n", temp);
						// check for extension bit
						if (handshakebuffer[25] & 0x10)
						{
							//char extensionhandshake[2048];
							unsigned char msgbuffer[MSGBUFFERSIZE];
							int msgsize = 0;
							int sent = 0;

							WritetoBTwindow("Extension bit is set!\n");
							// clear buffer
							memset(&msgbuffer[0], 0x0, MSGBUFFERSIZE); 
							// send extension handshake
							strcpy((char *)&msgbuffer[2], "d1:md11:ut_metadatai2ee13:metadata_sizei0ee");
							memset(&msgbuffer[0], 0x14, 1); // message id 20
							memset(&msgbuffer[1], 0x0, 1); // handshake id 0
							msgsize = 45;
							sent = SendBTmsg(peer_sock_fd, (char *) &msgbuffer[0], msgsize);
							if (sent == msgsize)
							{
								int got = 0;

								WritetoBTwindow("Sent %i bytes for extension handshake.\n", sent);
								// clear buffer
								memset(&msgbuffer[0], 0x0, MSGBUFFERSIZE); 
								// get extended handshake back
								got = GetBTmsg(peer_sock_fd, (char *) &msgbuffer, 20, 0);
								if (got > 0)
								{
									const char *ptr;

									WritetoBTwindow("Received %i bytes for extension handshake.\n", got);
									ptr = memmem(&msgbuffer[2], got - 2, "11:ut_metadatai", 15);
									if (ptr)
									{
										long msg_id = -1;
										const char *ptr2;

										// get ut_metadata value
										msg_id = atol((char*) ptr + 15);
										WritetoBTwindow("ut_metadata = %ld\n", msg_id);
										// get metadata_size
										ptr2 = memmem(&msgbuffer[2], got - 2, "13:metadata_sizei", 17);
										if (ptr2)
										{
											long metasize = -1;
											int numofpieces = 0;
											int lastpiecesize = 0;
											int counter = 0;
											char filename[PATH_MAX];
											int  torrentfile_fd;
											time_t currenttime;
											struct tm tm;

											// get metadata size
											metasize = atol((char*) ptr2 + 17);
											WritetoBTwindow("metadata_size = %ld\n", metasize);
											numofpieces = metasize / 16384;
											if ((lastpiecesize = metasize % 16384) > 0) numofpieces++;
											WritetoBTwindow("Number of pieces = %d\n", numofpieces);
											WritetoBTwindow("Last piece size = %d\n", lastpiecesize);
											// open torrentfile;
											currenttime = time(NULL);
											tm = *localtime(&currenttime);
											// use current time as filename
											sprintf(filename, "%s%04d%02d%02d%02d%02d%02d.torrent", 
											        torrentdir,
											        tm.tm_year + 1900, 
											        tm.tm_mon + 1, 
											        tm.tm_mday, 
											        tm.tm_hour, 
											        tm.tm_min, 
											        tm.tm_sec);
											// create file
											torrentfile_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC,
											                      S_IRWXU | S_IRWXG | S_IRWXO);
											write(torrentfile_fd, "d4:info", 7);
											// get pieces
											while (counter < numofpieces)
											{
												int sent2 = 0;
												int msgsize2 = 0;

												WritetoBTwindow("Attempting to get metadata piece %i\n", counter);
												// clear buffer
												memset(&msgbuffer[0], 0x0, MSGBUFFERSIZE); 
												// send get piece message
												sprintf((char *)&msgbuffer[2], "d8:msg_typei0e5:piecei%iee", counter);
												memset(&msgbuffer[0], 0x14, 1); // message id 20
												memset(&msgbuffer[1], msg_id, 1); // ut_metadata id
												if (counter < 10) msgsize2 = 27;
												else msgsize2 = 28;
												sent2 = SendBTmsg(peer_sock_fd, (char *) &msgbuffer[0], msgsize2);
												if (sent2 == msgsize2)
												{
													int got2 = 0;

													WritetoBTwindow("Sent %i bytes for get metadata piece.\n", sent2);
													// clear buffer
													memset(&msgbuffer[0], 0x0, MSGBUFFERSIZE); 
													// get piece back
													got2 = GetBTmsg(peer_sock_fd, (char *) &msgbuffer, 20, 2);
													if (got2 > 0)
													{
														WritetoBTwindow("Received %i bytes for get metadata piece.\n", got2);
														// store piece

														// if last piece write lastpiecesize
														// 16384 otherwise
														if (counter == numofpieces - 1)
														{
															write(torrentfile_fd, &msgbuffer[got2-lastpiecesize], 
															      lastpiecesize);
														}
														else
														{
															write(torrentfile_fd, &msgbuffer[got2-16384], 16384);
														}
													}
													else
													{
														if (got2 == 0) WritetoBTwindow("Disconnected by peer.\n");
														else if (got2 == -2) WritetoBTwindow("Three strikes! You're out. Disconnecting.\n");
														else WritetoBTwindow("Get piece receive failed. Disconnecting.\n");
														close(torrentfile_fd);
														// exit while loop
														counter = numofpieces;
													}
												}
												else
												{
													if (sent2 == 0) WritetoBTwindow("Disconnected by peer.\n");
													else WritetoBTwindow("Get piece send failed. Disconnecting.\n");
													close(torrentfile_fd);
													// exit while loop
													counter = numofpieces;
												}
												counter++;
											}
											write(torrentfile_fd, "e", 1);
											fsync(torrentfile_fd);
											close(torrentfile_fd);
											if (counter == numofpieces + 1)
												remove(filename);
											else
												WritetoBTwindow("Metadata captured! Disconnecting.\n");
										}
										else
										{
											WritetoBTwindow("Unable to get metadata size. Disconnecting.\n");
										}
									}
									else
									{
										WritetoBTwindow("ut_metadata not supported. Disconnecting.\n");
									}
								}
								else
								{
									if (got == 0) WritetoBTwindow("Disconnected by peer.\n");
									else if (got == -2) WritetoBTwindow("Three strikes! You're out. Disconnecting.\n");
									else WritetoBTwindow("Extension handshake received failed. Disconnecting.\n");
								}
							}
							else
							{
								if (sent == 0) WritetoBTwindow("Disconnected by peer.\n");
								else WritetoBTwindow("Extension handshake send failed. Disconnecting.\n");
							}
						}
						else
						{
							WritetoBTwindow("Peer dosen't support extension bit. Disconnecting.\n");
						}
					}
					else
					{
						if (hshake_recd == 0)
							WritetoBTwindow("Disconnected by peer.\n");
						else
							WritetoBTwindow("Handshake not received. Disconnecting.\n");
					}
				}
				else
				{
					if (hshake_sent == 0)
						WritetoBTwindow("Disconnected by peer.\n");
					else
						WritetoBTwindow("Handshake send failed. Disconnecting.\n");
				}
			}
			else {

				WritetoBTwindow("Peer connect from slot %i failed.\n", peerlistnum);
			}
			close(peer_sock_fd);
			// clear slot
			memset(&PeerListQueue[peerlistnum], 0, sizeof(struct peerlist));
		}
		sleep(1);
		// keep looking through peers
		peerlistnum++;
		if (peerlistnum == MAX_PEERPROSPECTS) peerlistnum = 0;
	}
	return 0; 
}

/****************************************************************************
 *                                                                          *
 * Function: CaptureAnnounce                                                *
 *                                                                          *
 * Purpose : Called whenever DHT sees an announce peer. Puts info hash into *
 *           a PeerListQueue slot                                           *
 *                                                                          *
 ****************************************************************************/
void CaptureAnnounce(unsigned char *hash, const struct sockaddr *fromaddr, unsigned short prt)
{
	if (fromaddr->sa_family == AF_INET) 
	{
		char temp[100];
		unsigned short tmpport;
		struct sockaddr_in *temp_sin = (struct sockaddr_in*)fromaddr;

		tmpport = htons(prt);
		// add to address to peerlist queuue
		memcpy(&PeerListQueue[peerlistopenslot].info_hash[0], &hash[0], 20);
		memcpy(&PeerListQueue[peerlistopenslot].addr, &(temp_sin->sin_addr), 4);
		memcpy(&PeerListQueue[peerlistopenslot].prt, &tmpport, 2);
		inet_ntop(AF_INET, &PeerListQueue[peerlistopenslot].addr, 
		          &temp[0], sizeof(temp));
		WritetoBTwindow("Peer captured slot %i:  Address: %s  Port: %d\n", 
		                peerlistopenslot, temp,
		                ntohs(PeerListQueue[peerlistopenslot].prt));
		peerlistopenslot++;
		if (peerlistopenslot == MAX_PEERPROSPECTS) peerlistopenslot = 0;
	}
	else
	{
		WritetoBTwindow("Ipv6 Address - peer not captured.\n");
	}
}

/******************************************************************************
 *                                                                            *
 * Function: ReadDHTmsg                                                    *
 *                                                                            *
 * Purpose :                                                                  *
 *                                                                            *
 ******************************************************************************/
gboolean ReadDHTmsg (void)
{
	char c;
	int ctr = 0;
	char readbuffer[256];

	if (read (dhtpipe[0], &c, 1) == 1) // we have something to read
	{
		while ((c != '\n') && (c != '\0') && (ctr < 255))
		{
			readbuffer[ctr] = c;
			read (dhtpipe[0] ,&c, 1);
			ctr++;
		}
		readbuffer[ctr] = '\0';
		if (ctr > 0) 
		{
			GtkTextIter iter;
			GtkAdjustment *vadj;
			gchar *temp;

			// write to the textbuffer
			temp = g_strconcat(readbuffer, "\n", NULL);
			gtk_text_buffer_get_end_iter (DHTtextbuffer, &iter);
			gtk_text_buffer_insert (DHTtextbuffer, &iter, temp, -1);

			// scroll to bottom
			vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW
			                                           (DHTWindowScrollView));
			gtk_adjustment_set_value (GTK_ADJUSTMENT(vadj), gtk_adjustment_get_upper 
			                          (GTK_ADJUSTMENT(vadj)));
			gtk_scrolled_window_set_vadjustment(GTK_SCROLLED_WINDOW
			                                    (DHTWindowScrollView), 
			                                    GTK_ADJUSTMENT (vadj));
		}
	}
	return TRUE; // keep running
}

/****************************************************************************
 *                                                                          *
 * Function: callback                                                       *
 *                                                                          *
 * Purpose : The call-back function is called by the DHT whenever something *
 *           interesting happens.  Right now, it only happens when we get a *
 *           new value or when a search completes, but this may be extended *
 *           in future versions.                                            *
 *                                                                          *
 ****************************************************************************/
static void callback(void *closure,
                     int event,
                     unsigned char *info_hash,
                     void *data, size_t data_len)
{

}

/****************************************************************************
 *                                                                          *
 * Function: dht_blacklisted                                                *
 *                                                                          *
 * Purpose :                                                                *
 *                                                                          *
 ****************************************************************************/
int dht_blacklisted(const struct sockaddr *sa, int salen)
{
	return 0;
}

/****************************************************************************
 *                                                                          *
 * Function: dht_hash                                                       *
 *                                                                          *
 * Purpose :                                                                *
 *                                                                          *
 ****************************************************************************/
void dht_hash(void *hash_return, int hash_size,
              const void *v1, int len1,
              const void *v2, int len2,
              const void *v3, int len3)
{
	const char *c1 = v1, *c2 = v2, *c3 = v3;
	char key[9];                /* crypt is limited to 8 characters */
	int i;

	memset(key, 0, 9);

	for(i = 0; i < 2 && i < len1; i++)
		key[i] = CRYPT_HAPPY(c1[i]);
	for(i = 0; i < 4 && i < len1; i++)
		key[2 + i] = CRYPT_HAPPY(c2[i]);
	for(i = 0; i < 2 && i < len1; i++)
		key[6 + i] = CRYPT_HAPPY(c3[i]);
	strncpy(hash_return, crypt(key, "dd"), hash_size);
}

/****************************************************************************
 *                                                                          *
 * Function: dht_random_bytes                                               *
 *                                                                          *
 * Purpose :                                                                *
 *                                                                          *
 ****************************************************************************/
int dht_random_bytes(void *buf, size_t size)
{
	int fd, rc, save;

	fd = open("/dev/urandom", O_RDONLY);
	if(fd < 0) return -1;
	rc = read(fd, buf, size);
	save = errno;
	close(fd);
	errno = save;
	return rc;
}

/****************************************************************************
 *                                                                          *
 * Function: DhtThread                                                      *
 *                                                                          *
 * Purpose :                                                                *
 *                                                                          *
 ****************************************************************************/
gint DhtThread(void)
{
	unsigned char myid[20];
	unsigned char seedid[20];
	int port;
	struct sockaddr_in sin;
	struct sockaddr_in6 sin6;
	struct sockaddr_in Seed;
	struct sockaddr_in6 Seed6;
	time_t tosleep = 0;
	int rc;
	struct sockaddr_storage from;
	socklen_t fromlen;

	// create random ids
	dht_random_bytes(myid, 20);
	dht_random_bytes(seedid, 20);

	// set random port between 2000 - 65535
	port = rand() % 63536 + 2000;
	fprintf(dht_debug, "DHT port set to: %d.\n", port);
	fflush(dht_debug);

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	s = socket(PF_INET, SOCK_DGRAM, 0);
	if(s < 0) 
	{
		fprintf(dht_debug, "IPv4 socket failed.\n");
		fflush(dht_debug);
	}
	if(s >= 0) 
	{
		if (!bind(s, (struct sockaddr*)&sin, sizeof(sin)))
		{
			fprintf(dht_debug, "IPv4 socket bound to dht port.\n");
			fflush(dht_debug);
		}
	}
	memset(&sin6, 0, sizeof(sin6));
	sin6.sin6_family = AF_INET6;
	sin6.sin6_port = htons(port);
	s6 = socket(PF_INET6, SOCK_DGRAM, 0);
	if(s6 < 0) {
		fprintf(dht_debug, "IPv6 socket failed.\n");
		fflush(dht_debug);
	}
	if(s6 >= 0) {
		int val = 1;

		setsockopt(s6, IPPROTO_IPV6, IPV6_V6ONLY,
		           (char *)&val, sizeof(val));
		if (!bind(s6, (struct sockaddr*)&sin6, sizeof(sin6)))
		{
			fprintf(dht_debug, "IPv6 socket bound to dht port.\n");
			fflush(dht_debug);
		}
	}
	if ((s < 0) && (s6 < 0))
	{
		fprintf(dht_debug, "No DHT sockets setup! Exiting....\n");
		fflush(dht_debug);
		return 0;
	}

	// init dht
	dht_init(s, s6, myid, (unsigned char*)"digg");

	if(s >= 0) 
	{
		if (Bootstrap.numofIPv4s > 0)
		{
			// ping saved nodes here
			int ctr = 0;

			for (ctr = 0; ctr < Bootstrap.numofIPv4s;  ctr++)
			{
				fprintf(dht_debug, "Pinging saved IPv4 node.\n");
				fflush(dht_debug);
				dht_ping_node( (struct sockaddr*) &Bootstrap.IPv4bootnodes[ctr], 
				              sizeof(struct sockaddr_in));
			}
		}
		fprintf(dht_debug, "Seeding IPv4 with dht.transmissionbt.com node.\n");
		fflush(dht_debug);
		memset(&Seed, 0, sizeof(Seed));
		Seed.sin_family = AF_INET;         
		Seed.sin_port = htons(6881); 
		inet_pton(AF_INET, "87.98.162.88", &(Seed.sin_addr.s_addr));
		dht_insert_node(seedid, (struct sockaddr*) &Seed, sizeof(Seed));
	}

	if(s6 >= 0)
	{

		if (Bootstrap.numofIPv6s > 0)
		{
			// ping saved nodes here
			int ctr = 0;

			for (ctr = 0; ctr < Bootstrap.numofIPv6s;  ctr++)
			{
				fprintf(dht_debug, "Pinging saved IPv6 node.\n");
				fflush(dht_debug);
				dht_ping_node((struct sockaddr*) &Bootstrap.IPv6bootnodes[ctr], 
				              sizeof(struct sockaddr_in6));
			}
		}
		fprintf(dht_debug, "Seeding IPv6 with dht.transmissionbt.com node.\n");
		fflush(dht_debug);
		memset(&Seed6, 0, sizeof(Seed6));
		Seed6.sin6_family = AF_INET6;         
		Seed6.sin6_port = htons(6881); 
		inet_pton(AF_INET6, "2001:41d0:c:5ac:5::1", &(Seed6.sin6_addr));
		dht_insert_node(seedid, (struct sockaddr*) &Seed6, sizeof(Seed6));
	}
	// start dht loop
	while(dhtloop) 
	{
		struct timeval tv;
		fd_set readfds;
		tv.tv_sec = tosleep;
		tv.tv_usec = random() % 1000000;

		FD_ZERO(&readfds);
		if(s >= 0)
			FD_SET(s, &readfds);
		if(s6 >= 0)
			FD_SET(s6, &readfds);
		rc = select(s > s6 ? s + 1 : s6 + 1, &readfds, NULL, NULL, &tv);
		if(rc < 0) {
			if(errno != EINTR) {
				perror("select");
				sleep(1);
			}
		}
		if(rc > 0) {
			fromlen = sizeof(from);
			if(s >= 0 && FD_ISSET(s, &readfds))
				rc = recvfrom(s, buf, sizeof(buf) - 1, 0,
				              (struct sockaddr*)&from, &fromlen);
			else if(s6 >= 0 && FD_ISSET(s6, &readfds))
				rc = recvfrom(s6, buf, sizeof(buf) - 1, 0,
				              (struct sockaddr*)&from, &fromlen);
			else
				abort();
		}
		if(rc > 0) {
			buf[rc] = '\0';
			rc = dht_periodic(buf, rc, (struct sockaddr*)&from, fromlen,
			                  &tosleep, (dht_callback *) callback, NULL);
		} else {
			rc = dht_periodic(NULL, 0, NULL, 0, &tosleep, (dht_callback *) callback, NULL);
		}
		if(rc < 0) {
			if(errno == EINTR) {
				continue;
			} else {
				perror("dht_periodic");
				if(rc == EINVAL || rc == EFAULT)
					abort();
				tosleep = 1;
			}
		}
	}
	dht_uninit ();
	close(s);
	close(s6);
	return 0; 
}

/****************************************************************************
 *                                                                          *
 * Function: MainWindowDestroy                                              *
 *                                                                          *
 * Purpose : Called when the window is closed                               *
 *                                                                          *
 ****************************************************************************/
void MainWindowDestroy (GtkWidget *widget, gpointer data)
{
	dhtloop = FALSE;
	getmetadataloop = FALSE;
	gtk_main_quit ();
}
/****************************************************************************
 *                                                                          *
 * Function: CreateMainWindow                                               *
 *                                                                          *
 * Purpose :                                                                *
 *                                                                          *
 ****************************************************************************/
static GtkWidget* CreateMainWindow (void)
{
	GtkWidget *window;
	GtkBuilder *builder;
	GError* error = NULL;

	// Load UI from file
	builder = gtk_builder_new ();
	if (gtk_builder_add_from_file (builder, UI_FILE, &error))
	{
		// Auto-connect signal handlers
		gtk_builder_connect_signals (builder, NULL);
		// Get the window objects from the ui file
		window = GTK_WIDGET (gtk_builder_get_object (builder, "window"));
		DHTtextbuffer = GTK_TEXT_BUFFER (gtk_builder_get_object 
		                                 (builder, "textbuffer1"));
		BTtextbuffer = GTK_TEXT_BUFFER (gtk_builder_get_object 
		                                (builder, "textbuffer2"));
		DHTWindowScrollView = GTK_SCROLLED_WINDOW (gtk_builder_get_object 
		                                           (builder, "scrolledwindow1"));
		BTWindowScrollView = GTK_SCROLLED_WINDOW (gtk_builder_get_object 
		                                          (builder, "scrolledwindow2"));
		// unload builder
		g_object_unref (builder);
	}
	else
	{
		g_print ("Unable to load dhtdigg.ui. Reinstall dhtdigg.\n"); 
		exit(1);
	}
	return window;
}

/****************************************************************************
 *                                                                          *
 * Function: main                                                           *
 *                                                                          *
 * Purpose :                                                                *
 *                                                                          *
 ****************************************************************************/
int main (int argc, char *argv[])
{
	GtkWidget *window;
	int flags;

	// init gtk
	gtk_init (&argc, &argv);
	// create main window
	window = CreateMainWindow ();
	// show main window
	gtk_widget_show (window);
	// seed rand
	srand((unsigned) time(NULL));
	// create pipe from dht to gui
	pipe(dhtpipe);
	// set read side to non-blocking
	flags = fcntl(dhtpipe[0], F_GETFL, 0);
	fcntl(dhtpipe[0], F_SETFL, flags | O_NONBLOCK);
	// redirect dht_debug to the pipe
	dht_debug = fdopen(dhtpipe[1], "w");
	// clear queues
	memset(&PeerListQueue, 0, sizeof(PeerListQueue));
	memset(&Bootstrap, 0, sizeof(Bootstrap));

	// set our torrent directory
	torrentdir = g_strconcat (g_get_home_dir (), "/.dhtdigg/", NULL);
	// make sure work directory exists
	if (stat(torrentdir, &st) == -1) mkdir(torrentdir, 0700);
	WritetoBTwindow("Setting torrent directory to %s\nWaiting for DHT to populate. This could take 10 minutes or so....\n", torrentdir);

	// start dht thread
	g_thread_new ("dhtthread", (GThreadFunc) DhtThread, NULL);
	// start read message thread
	gdk_threads_add_timeout (50, (GSourceFunc) ReadDHTmsg, NULL);
	// start getmetadata thread
	g_thread_new ("getmetadatathread", (GThreadFunc) GetMetadataThread, NULL);
	// dht restart timer 32 minutes
	gdk_threads_add_timeout_seconds (1920, (GSourceFunc) RestartDHT, NULL);
	// start main loop
	gtk_main ();
	return 0;
}

