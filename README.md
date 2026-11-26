# rtl8153_userspace

Test for "bringing up" an RTL8153 from userspace only (no drivers).  This makes it easier to do unusual network operations that are not trivial with Linux or Android, but instead allows apps to use layer 2 network access.

On the host PC, this uses the RTL8153 in CDC-ECM mode, and on Android, it uses the Realtek propreitary mode, as there is no way to claim CDC-ECM interfaces on Android.

This demo sets up a joke VPN service between the ethernet port coming out of your phone and an ethernet device connected to a linux box to be a "literal lan port" to another network. Please note that this code isn't perfect, or super well understood but it was enough for me to use youtube and other random internet stuff through. But please keep in mind the security is a joke using a symmetric chacha20 key.  Please don't use this as a VPN, but instead use it as a reference for some info on using the RTL8153.
