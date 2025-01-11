# rtl8153_userspace

Test for "bringing up" an RTL8153 from userspace only (no drivers).  This makes it easier to do unusual network operations that are not trivial with Linux or Android, but instead allows apps to use layer 2 network access.

On the host PC, this uses the RTL8153 in CDC-ECM mode, and on Android, it uses the Realtek propreitary mode, as there is no way to claim CDC-ECM interfaces on Android.


