build_inf.exe uniata2k.in_ > uniata2k.inf
copy uniata2k.in_ uniata_nt4.inf
srchrep -src ";DEVLIST" -dest "%%DeviceDesc%%" uniata_nt4.inf
srchrep -src ";DEVNAMELIST" -dest "DeviceDesc" uniata_nt4.inf
