#include "stdafx.h"
#include "DeinterlaceInputPin.h"
#include "DeinterlaceFilter.h"
#include "CPU.h"
#include "memcpy.h"
#include "resource.h"
#include "DI_Plugin.h"

/////////////////////////////////////////////////////////////////////////////
// CDeinterlaceFilter Constructor
/////////////////////////////////////////////////////////////////////////////

//Tee changed IDC_WEAVE to IDC_TYPE1 Two Frame 
CDeinterlaceFilter::CDeinterlaceFilter(TCHAR* tszName, LPUNKNOWN punk, HRESULT* phr) :
    CTransformFilter(tszName, punk, CLSID_Deinterlace),
    m_DeinterlaceType(IDC_TYPE1),
    CPersistStream(punk, phr),
    m_pDeinterlacePlugin(NULL),
    m_bIsOddFieldFirst(TRUE),
    m_History(0)
{
    memset(&m_Info, 0, sizeof(m_Info));
}


/////////////////////////////////////////////////////////////////////////////
// CreateInstance
// Provide the way for COM to create a Deinterlace object
/////////////////////////////////////////////////////////////////////////////
CUnknown* CDeinterlaceFilter::CreateInstance(LPUNKNOWN punk, HRESULT* phr)
{

#ifdef  _DEBUG
// uncomment to allow debug break point when filter is created
//  #pragma message (REMIND("INT 3 on entry for DEBUG included"))
//    __asm int 3
#endif

    CDeinterlaceFilter* pNewObject = new CDeinterlaceFilter(NAME("SMT Deint"), punk, phr);
    if (pNewObject == NULL)
    {
       *phr = E_OUTOFMEMORY;
    }
    return pNewObject;
}

/////////////////////////////////////////////////////////////////////////////
// GetPin
/////////////////////////////////////////////////////////////////////////////
CBasePin* CDeinterlaceFilter::GetPin(int n)
{
    HRESULT hr = S_OK;

    // Create an input pin if not already done

    if (m_pInput == NULL) 
    {
        m_pInput = new CDeinterlaceInputPin( 
                                                NAME("Input"),
                                                this,        // Owner filter
                                                &hr,         // Result code
                                                L"Input"     // Pin name
                                           );

        // Constructor for CTransInPlaceInputPin can't fail
        ASSERT(SUCCEEDED(hr));
    }

    // Create an output pin if not already done

    if (m_pInput!=NULL && m_pOutput == NULL)
    {
        m_pOutput = new CTransformOutputPin( 
                                                NAME("Output"),
                                                this,       // Owner filter
                                                &hr,        // Result code
                                                L"Output"   // Pin name
                                           );

        // a failed return code should delete the object
        ASSERT(SUCCEEDED(hr));
        if (m_pOutput == NULL) 
        {
            delete m_pInput;
            m_pInput = NULL;
        }
    }

    // Return the appropriate pin

    ASSERT (n>=0 && n<=1);
    if (n == 0) 
    {
        return m_pInput;
    } 
    else if (n==1) 
    {
        return m_pOutput;
    }
    else 
    {
        return NULL;
    }

} // GetPin

/////////////////////////////////////////////////////////////////////////////
// Start Streaming
/////////////////////////////////////////////////////////////////////////////
HRESULT CDeinterlaceFilter::StartStreaming()
{
    for (int i = 0;i < MAX_FRAMES_IN_HISTORY;i++)
    {
        m_pInputHistory[i] = NULL;
    }

    if(m_DeinterlaceType == IDC_TYPE4 && m_PlugInName != NULL)
    {
        // convert BSTR to a char string so that we
        // can use it to load plugin on all
        // platforms
        char DllName[MAX_PATH];
        wchar_t* pInStr = m_PlugInName;
        char* pOutStr = DllName;
        while(*(pOutStr++) = (char)(*pInStr++));

        m_pDeinterlacePlugin = new CDeinterlacePlugin(DllName);
        if (m_pDeinterlacePlugin)
        {
            m_pDeinterlacePlugin->StartStreaming();
        }
    }

    /////////////////////////////////////////////////////////
    // Set up Info structure
    // do one time setup here
    // this stuff shoudn't change
    /////////////////////////////////////////////////////////
    memset(&m_Info, 0, sizeof(m_Info));

    // Get input BITMAPINFOHEADER
    LPBITMAPINFOHEADER pbiIn;
    if (IsEqualGUID(*m_pInput->CurrentMediaType().FormatType(), FORMAT_VideoInfo)) 
    {
        pbiIn = &(((VIDEOINFOHEADER*)m_pInput->CurrentMediaType().Format())->bmiHeader);
    }
    else if (IsEqualGUID(*m_pInput->CurrentMediaType().FormatType(), FORMAT_VIDEOINFO2)) 
    {
        pbiIn = &(((VIDEOINFOHEADER2*)m_pInput->CurrentMediaType().Format())->bmiHeader);
    }

    // Generate output buffer pointers
    m_Info.LineLength = pbiIn->biWidth*pbiIn->biBitCount/8;
    m_Info.FrameWidth = pbiIn->biWidth;
    m_Info.FrameHeight = pbiIn->biHeight;
    m_Info.FieldHeight = pbiIn->biHeight / 2;
    if (m_Info.FieldHeight > MAX_INPUT_LINES_PER_FIELD) 
    {
        m_Info.FieldHeight = MAX_INPUT_LINES_PER_FIELD;
    }

    // Get output BITMAPINFOHEADER
    LPBITMAPINFOHEADER pbiOut;
    if (IsEqualGUID(*m_pOutput->CurrentMediaType().FormatType(), FORMAT_VideoInfo)) 
    {
        pbiOut = &(((VIDEOINFOHEADER*)m_pOutput->CurrentMediaType().Format())->bmiHeader);
    }
    else if (IsEqualGUID(*m_pOutput->CurrentMediaType().FormatType(), FORMAT_VIDEOINFO2)) 
    {
        pbiOut = &(((VIDEOINFOHEADER2*)m_pOutput->CurrentMediaType().Format())->bmiHeader);
    }
    m_Info.OverlayPitch = pbiOut->biWidth*pbiOut->biBitCount/8;

    m_Info.FieldDiff = -1;
    m_Info.CombFactor = -1;

    m_Info.CpuFeatureFlags = get_feature_flags();
    if(m_Info.CpuFeatureFlags & FEATURE_SSE) 
    {
        m_Info.pMemcpy = memcpySSE;
    }
    else 
    {
        m_Info.pMemcpy = memcpyMMX;
    }

    m_Info.OddLines[0] = m_OddLines[0];
    m_Info.EvenLines[0] = m_EvenLines[0];
    m_Info.OddLines[1] = m_OddLines[1];
    m_Info.EvenLines[1] = m_EvenLines[1];

    m_History = 0;
    m_LastStop = -1;

    return CTransformFilter::StartStreaming();
}

/////////////////////////////////////////////////////////////////////////////
// Stop Streaming
// Release anything we have created during processing
/////////////////////////////////////////////////////////////////////////////
HRESULT CDeinterlaceFilter::StopStreaming()
{
    for (int i = 0;i < MAX_FRAMES_IN_HISTORY;i++) 
    {
        m_pInputHistory[i] = NULL;
    }

    if (m_pDeinterlacePlugin) 
    {
        m_pDeinterlacePlugin->StopStreaming();
        delete m_pDeinterlacePlugin;
        m_pDeinterlacePlugin = NULL;
    }

    return CTransformFilter::StopStreaming();
}

/////////////////////////////////////////////////////////////////////////////
// NonDelegatingQueryInterface
// Reveals IDeinterlace, ISpecifyPropertyPages, IPersistStream
/////////////////////////////////////////////////////////////////////////////
STDMETHODIMP CDeinterlaceFilter::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
    CheckPointer(ppv,E_POINTER);

    if (riid == IID_IDeinterlace)
    {
        return GetInterface((IDeinterlace*) this, ppv);
    }
    else if (riid == IID_ISpecifyPropertyPages)
    {
        return GetInterface((ISpecifyPropertyPages*) this, ppv);
    }
    else if (riid == IID_IPersistStream)
    {
        return GetInterface((IPersistStream*) this, ppv);
    }
    else
    {
        return CTransformFilter::NonDelegatingQueryInterface(riid, ppv);
    }
}


  
/////////////////////////////////////////////////////////////////////////////
// Transform
// Actually do processing on data
// This is called from within the base classes
/////////////////////////////////////////////////////////////////////////////
long BitShift = 13;
long EdgeDetect = 625;
long JaggieThreshold = 73;
long DiffThreshold = 224;
long TemporalTolerance = 300;
long SpatialTolerance = 600;
long SimilarityThreshold = 25;

// Set up our output sample
HRESULT CDeinterlaceFilter::GetOutputSampleBuffer(IMediaSample* pSample, IMediaSample** ppOutput)
{
    IMediaSample* pOutSample;

    // default - times are the same
    AM_SAMPLE2_PROPERTIES*  const pProps = m_pInput->SampleProps();
    DWORD dwFlags = m_bSampleSkipped ? AM_GBF_PREVFRAMESKIPPED : 0;

    // This will prevent the image renderer from switching us to DirectDraw
    // when we can't do it without skipping frames because we're not on a
    // keyframe.  If it really has to switch us, it still will, but then we
    // will have to wait for the next keyframe
    if (!(pProps->dwSampleFlags & AM_SAMPLE_SPLICEPOINT)) 
    {
        dwFlags |= AM_GBF_NOTASYNCPOINT;
    }

    HRESULT hr = m_pOutput->GetDeliveryBuffer(&pOutSample,
        pProps->dwSampleFlags & AM_SAMPLE_TIMEVALID ? &pProps->tStart : NULL,
        pProps->dwSampleFlags & AM_SAMPLE_STOPVALID ? &pProps->tStop : NULL,
        dwFlags);

   * ppOutput = pOutSample;
    if (FAILED(hr)) 
    {
       * ppOutput = NULL;
        return hr;
    }

    //
    // Check if output media type has been changed.
    // Downstream filter may ask us to output data to
    // a larger bitmap - so usually does Overlay Mixer,
    // or to a different format - so does Video Renderer.
    // Since we accept only YUY2, we primarily take care of
    // the former.
    //
    AM_MEDIA_TYPE* pmt;
    pOutSample->GetMediaType(&pmt);
    if (pmt != NULL) 
    {
        CMediaType cmt(*pmt);
        DeleteMediaType(pmt);
        m_pOutput->SetMediaType(&cmt);
    }

    //
    // Copy IMediaSample2 properties
    //
    ASSERT(pOutSample);
    CComQIPtr<IMediaSample2,&IID_IMediaSample2> pOutSample2(pOutSample);
    if (pOutSample2 != NULL) 
    {
        //  Modify it
        AM_SAMPLE2_PROPERTIES OutProps;
        EXECUTE_ASSERT(SUCCEEDED(pOutSample2->GetProperties(FIELD_OFFSET(AM_SAMPLE2_PROPERTIES, tStart), (PBYTE)&OutProps)));

        OutProps.dwTypeSpecificFlags = 0;   // Filter out interlace flags

        OutProps.dwSampleFlags =
            (OutProps.dwSampleFlags & AM_SAMPLE_TYPECHANGED) |
            (pProps->dwSampleFlags & ~AM_SAMPLE_TYPECHANGED);
        OutProps.tStart = pProps->tStart;
        OutProps.tStop  = pProps->tStop;
        OutProps.cbData = FIELD_OFFSET(AM_SAMPLE2_PROPERTIES, dwStreamId);
        hr = pOutSample2->SetProperties(FIELD_OFFSET(AM_SAMPLE2_PROPERTIES, dwStreamId), (PBYTE)&OutProps);

        if (pProps->dwSampleFlags & AM_SAMPLE_DATADISCONTINUITY) 
        {
            m_bSampleSkipped = FALSE;
        }
    } 
    else 
    {
        if (pProps->dwSampleFlags & AM_SAMPLE_TIMEVALID) 
        {
            pOutSample->SetTime(&pProps->tStart, &pProps->tStop);
        }
        if (pProps->dwSampleFlags & AM_SAMPLE_SPLICEPOINT) 
        {
            pOutSample->SetSyncPoint(TRUE);
        }
        if (pProps->dwSampleFlags & AM_SAMPLE_DATADISCONTINUITY) 
        {
            pOutSample->SetDiscontinuity(TRUE);
            m_bSampleSkipped = FALSE;
        }

        // Copy the media times
        // Will be updated later if needed
        LONGLONG MediaStart;
        LONGLONG MediaEnd;
        if (pSample->GetMediaTime(&MediaStart,&MediaEnd) == NOERROR) 
        {
            pOutSample->SetMediaTime(&MediaStart,&MediaEnd);
        }
    }
    return S_OK;
}

//
// Process input samples
//
HRESULT CDeinterlaceFilter::Receive(IMediaSample* pSample)
{
    //
    // Check for other streams and pass them on
    //
    AM_SAMPLE2_PROPERTIES*  const pProps = m_pInput->SampleProps();
    if (pProps->dwStreamId != AM_STREAM_MEDIA) 
    {
        return m_pOutput->Deliver(pSample);
    }
    HRESULT hr;
    ASSERT(pSample);

    // If no output to deliver to then no point sending us data
    ASSERT(m_pOutput != NULL);

    // Process input sample and deliver output sample(s)
    hr = Deinterlace(pSample);

    return hr;
}

//
// Deinterlace input frames/fields and generate output samples
//
HRESULT CDeinterlaceFilter::Deinterlace(IMediaSample* pSource)
{
    CAutoLock l(&m_DeinterlaceLock);
    int i;
    REFERENCE_TIME rtStart;
    REFERENCE_TIME rtStop;
    BYTE* pSourceBuffer;

    // Get the input stream times
    if(FAILED(pSource->GetTime(&rtStart,&rtStop)))
    {
        return E_FAIL;
    }

    // Check for stream continuity
    // note that the IsDiscontinuity
    // method appears not to work
    if (rtStart != m_LastStop) 
    {
        DbgLog((LOG_TRACE, 2, TEXT("Discontinity, flushing history buffer.")));
        m_History = 0;
        for (int i = 0;i < MAX_FRAMES_IN_HISTORY ;i++)
        {
            m_pInputHistory[i] = NULL;
        }
    }
    else
    {
        for (int i = 1; i < MAX_FRAMES_IN_HISTORY; i++)
        {
            m_pInputHistory[i - 1] = m_pInputHistory[i];
        }
    }
    m_pInputHistory[MAX_FRAMES_IN_HISTORY - 1] = pSource;
    m_LastStop = rtStop;

    pSource->GetPointer(&pSourceBuffer);
    
    AM_SAMPLE2_PROPERTIES*  const pProps = m_pInput->SampleProps();

    if(m_bIsOddFieldFirst)
    {
        for (i=0;i<m_Info.FieldHeight;i++) 
        {
            m_OddLines[1][i] = m_OddLines[0][i];
            m_OddLines[0][i] = (short*)(pSourceBuffer+(2*i+1)*m_Info.LineLength);
        }
        m_Info.IsOdd = TRUE;
    }
    else
    {
        for (i=0;i<m_Info.FieldHeight;i++) 
        {
            m_EvenLines[1][i] = m_EvenLines[0][i];
            m_EvenLines[0][i] = (short*)(pSourceBuffer+(2*i)*m_Info.LineLength);
        }
        m_Info.IsOdd = FALSE;
    }
    ++m_History;

   
    // Call deinterlace method and deliver samples
    //
    // Assume frame input and call deinterlace method
    // twice for each sample thus producing two
    // output frames

    // Generate first field
    IMediaSample* pDest;

    HRESULT hr = GetOutputSampleBuffer(pSource,&pDest);
    if (FAILED(hr))
    {
        return NOERROR;
    }

    BYTE* pDestBuffer;
    pDest->GetPointer(&pDestBuffer);

    m_Info.Overlay = pDestBuffer;

    if(m_History > 3)
    {
        CallDeinterlaceMethod(&m_Info);
    }
    else
    {
        Bob(&m_Info);
    }

    rtStop = rtStart + (rtStop - rtStart)/2;
    pDest->SetTime(&rtStart,&rtStop);

    m_pOutput->Deliver(pDest);

    pDest->Release();
    pDest = NULL;

    //
    // Generate second field
    //
    hr = GetOutputSampleBuffer(pSource,&pDest);
    if (FAILED(hr))
        return NOERROR;

    pDest->GetPointer(&pDestBuffer);

    m_Info.Overlay = pDestBuffer;

    if(m_bIsOddFieldFirst)
    {
        for (i=0;i<m_Info.FieldHeight;i++) 
        {
            m_EvenLines[1][i] = m_EvenLines[0][i];
            m_EvenLines[0][i] = (short*)(pSourceBuffer+(2*i)*m_Info.LineLength);
        }
        m_Info.IsOdd = FALSE;
    }
    else
    {
        for (i=0;i<m_Info.FieldHeight;i++) 
        {
            m_OddLines[1][i] = m_OddLines[0][i];
            m_OddLines[0][i] = (short*)(pSourceBuffer+(2*i+1)*m_Info.LineLength);
        }
        m_Info.IsOdd = TRUE;
    }
    ++m_History;

    if(m_History > 3)
    {
        CallDeinterlaceMethod(&m_Info);
    }
    else
    {
        DeinterlaceFieldBob(&m_Info);
    }

    // Time for second field
    rtStart = rtStart + (rtStop - rtStart + 1)/2;
    pDest->SetTime(&rtStart,&rtStop);

    m_pOutput->Deliver(pDest);
    pDest->Release();

	return NOERROR;
}

//
// Call apropriate deinterlace method
//
void CDeinterlaceFilter::CallDeinterlaceMethod(DEINTERLACE_INFO* pInfo) const
{
    switch (m_DeinterlaceType) 
    {
		case IDC_WEAVE:

		case IDC_BOB:
			DeinterlaceFieldBob(pInfo);
			break;
		case IDC_TYPE1:
			DeinterlaceFieldTwoFrame(pInfo);
			break;
		case IDC_TYPE2:
			BlendedClipping(pInfo);
			Weave(pInfo);
			break;
		case IDC_TYPE3:
			DeinterlaceFieldBob(pInfo);
			break;
		case IDC_TYPE4:
			m_pDeinterlacePlugin->Process(pInfo);
			break;
		default:
			DeinterlaceFieldWeave(pInfo);
			break;
    }
}

/////////////////////////////////////////////////////////////////////////////
// Copy
// Make destination properties the same as source
/////////////////////////////////////////////////////////////////////////////
//#if 0
HRESULT CDeinterlaceFilter::CopyProperties(IMediaSample* pSource, IMediaSample* pDest)
{
    // Copy the sample times

    REFERENCE_TIME TimeStart, TimeEnd;
    if (NOERROR == pSource->GetTime(&TimeStart, &TimeEnd))
    {
        pDest->SetTime(&TimeStart, &TimeEnd);
    }

    LONGLONG MediaStart, MediaEnd;
    if (pSource->GetMediaTime(&MediaStart,&MediaEnd) == NOERROR)
    {
        pDest->SetMediaTime(&MediaStart,&MediaEnd);
    }

    // Copy the Sync point property

    HRESULT hr = pSource->IsSyncPoint();
    if (hr == S_OK)
    {
        pDest->SetSyncPoint(TRUE);
    }
    else if (hr == S_FALSE)
    {
        pDest->SetSyncPoint(FALSE);
    }
    else
    {  
        // an unexpected error has occured...
        return E_UNEXPECTED;
    }

    // Copy the media type

    AM_MEDIA_TYPE* pMediaType;
    pSource->GetMediaType(&pMediaType);
    pDest->SetMediaType(pMediaType);
    DeleteMediaType(pMediaType);

    // Copy the preroll property

    hr = pSource->IsPreroll();
    if (hr == S_OK)
    {
        pDest->SetPreroll(TRUE);
    }
    else if (hr == S_FALSE)
    {
        pDest->SetPreroll(FALSE);
    }
    else
    {  // an unexpected error has occured...
        return E_UNEXPECTED;
    }

    // Copy the discontinuity property

    hr = pSource->IsDiscontinuity();
    if (hr == S_OK)
    {
        pDest->SetDiscontinuity(TRUE);
    }
    else if (hr == S_FALSE)
    {
        pDest->SetDiscontinuity(FALSE);
    }
    else
    {  
        // an unexpected error has occured...
        return E_UNEXPECTED;
    }

    // Copy the actual data length
    long lDataLength = pSource->GetActualDataLength();
    pDest->SetActualDataLength(lDataLength);

    return NOERROR;

}
//#endif

/////////////////////////////////////////////////////////////////////////////
// CheckInputType
// Check the input type is OK - return an error otherwise
/////////////////////////////////////////////////////////////////////////////
HRESULT CDeinterlaceFilter::CheckInputType(const CMediaType* mtIn)
{
    // check this is a VIDEOINFOHEADER type 
    // MAE Added VideoInfo back in
    if ((*mtIn->FormatType() != FORMAT_VideoInfo) && 
        (*mtIn->FormatType() != FORMAT_VIDEOINFO2))
    {
        return E_INVALIDARG;
    }

    // Can we transform this type
    if (CanPerformDeinterlace(mtIn))
    {
        return NOERROR;
    }
    return E_FAIL;
}


/////////////////////////////////////////////////////////////////////////////
// CheckTransform
// Check a transform can be done between these formats
/////////////////////////////////////////////////////////////////////////////
HRESULT CDeinterlaceFilter::CheckTransform(const CMediaType* pmtIn, const CMediaType* pmtOut)
{
    DbgLog((LOG_TRACE, 2, TEXT("CDeinterlaceFilter::CheckTransform")));

    // we only output video
    if (*pmtOut->Type() != MEDIATYPE_Video) 
    {
        return VFW_E_TYPE_NOT_ACCEPTED;
    }

    // Check there is a format block
    if (*pmtOut->FormatType() != FORMAT_VideoInfo &&
        *pmtOut->FormatType() != FORMAT_VIDEOINFO2) 
    {
        return VFW_E_TYPE_NOT_ACCEPTED;
    }

    if (!CanPerformDeinterlace(pmtIn))
    {
        return VFW_E_TYPE_NOT_ACCEPTED;
    }

    // See if we can use dci/direct draw.
    // First check that there is a non empty target rectangle.

    // Since we need only rectangles here, skip VIDEOINFOHEADER 2 cheking
    VIDEOINFO* videoInfo = (VIDEOINFO*)pmtOut->pbFormat;
    VIDEOINFO* videoInfoIn = (VIDEOINFO*)pmtIn->pbFormat;
    if (!IsRectEmpty(&videoInfo->rcTarget)) 
    {
        // Next, check that the source rectangle is the entire movie.
        if(videoInfo->rcSource.left == 0 && 
           videoInfo->rcSource.top == 0 && 
           videoInfo->rcSource.right == (videoInfoIn->rcSource.right-videoInfoIn->rcSource.left) &&
           videoInfo->rcSource.bottom == (videoInfoIn->rcSource.bottom-videoInfoIn->rcSource.top))
        {
            // Now check that the target rectangles size is the same as
            // the movies, that is there is no stretching or shrinking.
            if((videoInfo->rcTarget.right - videoInfo->rcTarget.left) ==
                    (videoInfoIn->rcSource.right-videoInfoIn->rcSource.left) && 
               (videoInfo->rcTarget.bottom - videoInfo->rcTarget.top) ==
                    (videoInfoIn->rcSource.bottom-videoInfoIn->rcSource.top)) 
            {
#ifndef _X86_
                // On Risc machines make sure we are DWORD aligned
                if ((videoInfo->rcTarget.left & 0x03) == 0x00)
#endif
                {
                    DbgLog((LOG_TRACE, 2, TEXT("Using DCI")));
                    return S_OK;
                }
            }
        }
        DbgLog((LOG_TRACE, 2, TEXT("NOT Using DCI")));
        return E_FAIL;
    }

    return S_OK;
}


/////////////////////////////////////////////////////////////////////////////
// DecideBufferSize
// Tell the output pin's allocator what size buffers we
// require. Can only do this when the input is connected
/////////////////////////////////////////////////////////////////////////////
HRESULT CDeinterlaceFilter::DecideBufferSize(IMemAllocator* pAlloc,ALLOCATOR_PROPERTIES* pProperties)
{
    // Is the input pin connected
    if (m_pInput->IsConnected() == FALSE)
    {
        return E_UNEXPECTED;
    }

    ASSERT(pAlloc);
    ASSERT(pProperties);
    HRESULT hr = NOERROR;

    // Get input pin's allocator size and use that
    ALLOCATOR_PROPERTIES InProps;
    CComPtr<IMemAllocator> pInAlloc;
    hr = m_pInput->GetAllocator(&pInAlloc);
    if (SUCCEEDED (hr))
    {
        hr = pInAlloc->GetProperties (&InProps);
        if (SUCCEEDED (hr))
        {
            pProperties->cbBuffer = InProps.cbBuffer;
            pProperties->cBuffers = 1;
            //pProperties->cbAlign = InProps.cbAlign;
        }
        else
        {
            return hr;
        }
    }
    else
    {
        return hr;
    }

    ASSERT(pProperties->cbBuffer);
    //ASSERT(pProperties->cbAlign >= 8);

    // Ask the allocator to reserve us some sample memory, NOTE the function
    // can succeed (that is return NOERROR) but still not have allocated the
    // memory that we requested, so we must check we got whatever we wanted

    ALLOCATOR_PROPERTIES Actual;
    hr = pAlloc->GetProperties(&Actual);
    hr = pAlloc->SetProperties(pProperties,&Actual);
    if (FAILED(hr))
    {
        return hr;
    }

    ASSERT( Actual.cBuffers == 1 );

    if (pProperties->cBuffers > Actual.cBuffers || pProperties->cbBuffer > Actual.cbBuffer)
    {
        return E_FAIL;
    }
    return NOERROR;
}


/////////////////////////////////////////////////////////////////////////////
// GetMediaType
//
// I support one type, namely the type of the input pin
// This type is only available if my input is connected
/////////////////////////////////////////////////////////////////////////////
HRESULT CDeinterlaceFilter::GetMediaType(int iPosition, CMediaType* pMediaType)
{
    // Is the input pin connected
    if (m_pInput->IsConnected() == FALSE)
    {
        return E_UNEXPECTED;
    }

    // This should never happen
    if (iPosition < 0)
    {
        return E_INVALIDARG;
    }

    // Do we have more items to offer
    if (iPosition > 0)
    {
        return VFW_S_NO_MORE_ITEMS;
    }

    // copy format from input pin
    CMediaType cmt;
    cmt = m_pInput->CurrentMediaType();

    if(IsEqualGUID(*cmt.FormatType(), FORMAT_VIDEOINFO2)) 
    {
        VIDEOINFOHEADER2* pvi = (VIDEOINFOHEADER2*)cmt.Format();
        // we output 2 samples for each incoming sample
        //pvi->AvgTimePerFrame = pvi->AvgTimePerFrame/2;
        //pvi->dwBitRate = pvi->dwBitRate*2;
        pvi->dwInterlaceFlags = 0;  // progressive
    }
    else if(IsEqualGUID(*cmt.FormatType(), FORMAT_VideoInfo)) 
    {
        VIDEOINFOHEADER* pvi = (VIDEOINFOHEADER*)cmt.Format();
        // we output 2 samples for each incoming sample
        pvi->AvgTimePerFrame = pvi->AvgTimePerFrame/2;
        pvi->dwBitRate = pvi->dwBitRate*2;
    }

    *pMediaType = cmt;

    return NOERROR;

}


/////////////////////////////////////////////////////////////////////////////
// CanPerformDeinterlace
// Check if this is one of out supported formats and data has 24 bits
/////////////////////////////////////////////////////////////////////////////
BOOL CDeinterlaceFilter::CanPerformDeinterlace(const CMediaType* pMediaType) const
{
    if (IsEqualGUID(*pMediaType->Type(), MEDIATYPE_Video))
    {
        if (IsEqualGUID(*pMediaType->FormatType(), FORMAT_VIDEOINFO2)) 
        {
            VIDEOINFOHEADER2* pvi = (VIDEOINFOHEADER2*)pMediaType->Format();

            // Commented out for now - sometimes IsInterlaced is not set on interlaced material
            //
            // if (!(pvi->dwInterlaceFlags & AMINTERLACE_IsInterlaced)) 
            // {
            //     DbgLog((LOG_TRACE,1,TEXT("No need to deinterlace")));
            //     return FALSE;
            // }

            if (pvi->dwInterlaceFlags & AMINTERLACE_1FieldPerSample) 
            {
                DbgLog((LOG_ERROR,1,TEXT("Field samples are not supported yet")));
                return FALSE;
            }
        }

        LPBITMAPINFOHEADER pbi;
        if (IsEqualGUID(*pMediaType->FormatType(), FORMAT_VideoInfo)) 
        {
            pbi = &(((VIDEOINFOHEADER*)pMediaType->Format())->bmiHeader);
        }
        else if (IsEqualGUID(*pMediaType->FormatType(), FORMAT_VIDEOINFO2)) 
        {
            pbi = &(((VIDEOINFOHEADER2*)pMediaType->Format())->bmiHeader);
        }

        // Some deinterlace routines assume the input is YUY2
        // and produce incorrect results on RGB input
        //
        if (IsEqualGUID(*pMediaType->Subtype(), MEDIASUBTYPE_RGB24)) 
        {
            return (pbi->biBitCount == 24);
        }
        else if (IsEqualGUID(*pMediaType->Subtype(), MEDIASUBTYPE_RGB8)) 
        {
            return (pbi->biBitCount == 8);
        }
        else if(IsEqualGUID(*pMediaType->Subtype(), MEDIASUBTYPE_YUY2)) 
        {
            return (pbi->biBitCount == 16);
        }
    }
    return FALSE;
}


/////////////////////////////////////////////////////////////////////////////
// GetClassID
// This is the only method of IPersist
/////////////////////////////////////////////////////////////////////////////
STDMETHODIMP CDeinterlaceFilter::GetClassID(CLSID* pClsid)
{
    return CBaseFilter::GetClassID(pClsid);
}


/////////////////////////////////////////////////////////////////////////////
// ScribbleToStream
// Overriden to write our state into a stream
/////////////////////////////////////////////////////////////////////////////
HRESULT CDeinterlaceFilter::WriteToStream(IStream* pStream)
{
    int Version(120);
    pStream->Write(&Version, sizeof(Version), NULL);
    pStream->Write(&m_DeinterlaceType, sizeof(m_DeinterlaceType), NULL);
    pStream->Write(&m_bIsOddFieldFirst, sizeof(m_bIsOddFieldFirst), NULL);
    return m_PlugInName.WriteToStream(pStream);
}


/////////////////////////////////////////////////////////////////////////////
// ReadFromStream
// Likewise overriden to restore our state from a stream
/////////////////////////////////////////////////////////////////////////////
HRESULT CDeinterlaceFilter::ReadFromStream(IStream* pStream)
{
    wchar_t Name[MAX_PATH] = L"";
    wchar_t* ReadChar = Name;
    int Version(0);
    
    HRESULT hr = pStream->Read(&Version, sizeof(Version), NULL);
    if(FAILED(hr))
    {
        return hr;
    }
    
    // cope with old filter graph files
    if(Version != 120)
    {
        m_DeinterlaceType = Version;
        return hr;
    }
    
    hr = pStream->Read(&m_DeinterlaceType, sizeof(m_DeinterlaceType), NULL);
    if(FAILED(hr))
    {
        return hr;
    }
    hr = pStream->Read(&m_bIsOddFieldFirst, sizeof(m_bIsOddFieldFirst), NULL);
    if(FAILED(hr))
    {
        return hr;
    }

    hr = m_PlugInName.ReadFromStream(pStream);
    {
        return hr;
    }
    return S_OK;
}


/////////////////////////////////////////////////////////////////////////////
// GetPages
// Returns the clsid's of the property pages we support
/////////////////////////////////////////////////////////////////////////////
STDMETHODIMP CDeinterlaceFilter::GetPages(CAUUID* pPages)
{
    pPages->cElems = 2;
    pPages->pElems = (GUID*) CoTaskMemAlloc(2*sizeof(GUID));
    if (pPages->pElems == NULL)
    {
        return E_OUTOFMEMORY;
    }
    *(pPages->pElems + 1) = CLSID_DeinterlacePropertyPage;
    return NOERROR;
}


/////////////////////////////////////////////////////////////////////////////
// Inplement IDeinterlace Functions
/////////////////////////////////////////////////////////////////////////////
STDMETHODIMP CDeinterlaceFilter::get_DeinterlaceType(long* piType)
{
    CAutoLock cAutolock(&m_DeinterlaceLock);
    CheckPointer(piType,E_POINTER);
    *piType = m_DeinterlaceType;
    return NOERROR;
}


STDMETHODIMP CDeinterlaceFilter::put_DeinterlaceType(long iType)
{
    CAutoLock cAutolock(&m_DeinterlaceLock);
    m_DeinterlaceType = iType;
    SetDirty(TRUE);
    return NOERROR;
}

STDMETHODIMP CDeinterlaceFilter::get_IsOddFieldFirst(VARIANT_BOOL* pOddFirst)
{
    CAutoLock cAutolock(&m_DeinterlaceLock);
    CheckPointer(pOddFirst,E_POINTER);
    *pOddFirst = m_bIsOddFieldFirst?VARIANT_TRUE:VARIANT_FALSE;
    return NOERROR;
}

STDMETHODIMP CDeinterlaceFilter::put_IsOddFieldFirst(VARIANT_BOOL OddFirst)
{
    CAutoLock cAutolock(&m_DeinterlaceLock);
    m_bIsOddFieldFirst = (OddFirst != VARIANT_FALSE);
    return NOERROR;
}

STDMETHODIMP CDeinterlaceFilter::get_DScalerPluginName(BSTR* pPlugInName)
{
    CAutoLock cAutolock(&m_DeinterlaceLock);
    *pPlugInName = m_PlugInName.Copy();
    return NOERROR;
}

STDMETHODIMP CDeinterlaceFilter::put_DScalerPluginName(BSTR PlugInName)
{
    CAutoLock cAutolock(&m_DeinterlaceLock);
    m_PlugInName = PlugInName;
    return NOERROR;
}
