VERSION 5.00
Begin VB.Form Form1 
   Caption         =   "Form1"
   ClientHeight    =   12465
   ClientLeft      =   1650
   ClientTop       =   1545
   ClientWidth     =   6585
   LinkTopic       =   "Form1"
   ScaleHeight     =   12465
   ScaleWidth      =   6585
   Begin VB.CommandButton Command7 
      Caption         =   "Load driver"
      Height          =   615
      Left            =   600
      TabIndex        =   7
      Top             =   180
      Width           =   5115
   End
   Begin VB.CommandButton Command6 
      Caption         =   "Command6"
      Height          =   555
      Left            =   1680
      TabIndex        =   6
      Top             =   10140
      Width           =   3375
   End
   Begin VB.CommandButton Command5 
      Caption         =   "Read boot sector from drive"
      Height          =   495
      Left            =   540
      TabIndex        =   5
      Top             =   7920
      Width           =   5715
   End
   Begin VB.TextBox Text1 
      Height          =   375
      Left            =   780
      TabIndex        =   4
      Text            =   "Text1"
      Top             =   8580
      Width           =   5295
   End
   Begin VB.CommandButton Command4 
      Caption         =   "Load device from image file"
      Height          =   915
      Left            =   840
      TabIndex        =   3
      Top             =   6660
      Width           =   5415
   End
   Begin VB.CommandButton Command3 
      Caption         =   "Remove device"
      Height          =   735
      Left            =   1080
      TabIndex        =   2
      Top             =   4980
      Width           =   4275
   End
   Begin VB.CommandButton Command2 
      Caption         =   "Save to image file"
      Height          =   735
      Left            =   1080
      TabIndex        =   1
      Top             =   3180
      Width           =   4695
   End
   Begin VB.CommandButton Command1 
      Caption         =   "Create new device"
      Height          =   795
      Left            =   540
      TabIndex        =   0
      Top             =   1080
      Width           =   5355
   End
End
Attribute VB_Name = "Form1"
Attribute VB_GlobalNameSpace = False
Attribute VB_Creatable = False
Attribute VB_PredeclaredId = True
Attribute VB_Exposed = False
Option Explicit

Dim ImDisk As New ImDiskCOM

Private Sub Command1_Click()

  Dim DeviceNumber As Long
  DeviceNumber = -1
  
  Dim DiskSize As LARGE_INTEGER
  DiskSize.LowPart = 1440 * 2 ^ 10
  Dim ImageOffset As LARGE_INTEGER
  
  ImDisk.CreateDeviceEx DiskSize:=DiskSize, ImageOffset:=ImageOffset, MountPoint:="B:", DeviceNumber:=DeviceNumber, StatusControl:=Text1.hWnd
  
  Dim Flags As ImDiskFlags
  Dim DriveLetter As String
  Dim Filename As String
  
  ImDisk.QueryDevice _
    DeviceNumber, _
    DiskSize:=DiskSize, _
    ImageOffset:=ImageOffset, _
    Flags:=Flags, _
    DriveLetter:=DriveLetter, _
    Filename:=Filename
  
End Sub

Private Sub Command4_Click()

  Dim DeviceNumber As Long
  DeviceNumber = -1
  
  Dim Filename As String
  Filename = "C:\test.img"
  
  Dim DiskSize As LARGE_INTEGER
  Dim ImageOffset As LARGE_INTEGER
  
  ImDisk.AutoFindOffsetAndSize Filename, ImageOffset, DiskSize
  
  ImDisk.CreateDeviceEx DiskSize:=DiskSize, ImageOffset:=ImageOffset, Filename:=Filename, Flags:=ImDiskFlags_TypeVM, MountPoint:="B:", DeviceNumber:=DeviceNumber, StatusControl:=Text1.hWnd
  
  Dim Flags As ImDiskFlags
  Dim DriveLetter As String
  
  ImDisk.QueryDevice _
    DeviceNumber, _
    DiskSize:=DiskSize, _
    ImageOffset:=ImageOffset, _
    Flags:=Flags, _
    DriveLetter:=DriveLetter, _
    Filename:=Filename
  
End Sub

Private Sub Command2_Click()

  Dim Device As ImDiskDevice
  Set Device = ImDisk.OpenDeviceByMountPoint("B:", FileAccess_Read)
  
  Device.SaveImageFile "C:\test.img"
   
  Device.Close
  
End Sub

Private Sub Command3_Click()

  ImDisk.RemoveDeviceByMountPoint "B:", StatusControl:=Text1.hWnd
  
End Sub

Private Sub Command5_Click()

  Dim Device As ImDiskDevice
  Set Device = ImDisk.OpenDeviceByMountPoint("B:", FileAccess_Read)
  
  Dim Stream As Stream
  Set Stream = Device.GetRawDiskStream()
  
  Text1 = ""
  Dim i As Integer
  For i = 0 To 511
    Dim hexstr As String
    hexstr = Hex(Stream.ReadByte())
    If Len(hexstr) = 1 Then
      hexstr = "0" & hexstr
    End If
    Text1 = Text1 & hexstr
  Next
  
  Device.Close
  
End Sub

Private Sub Command6_Click()
  
  MkDir "C:\test"
  ImDisk.CreateMountPointForDeviceNumber "C:\test", 0

End Sub

Private Sub Command7_Click()

  ImDisk.LoadDriver
  ImDisk.LoadHelperService

End Sub
