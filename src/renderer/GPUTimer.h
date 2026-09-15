#pragma once
#include <d3d11.h>
#include <winrt/base.h>
#include <array>
#include <optional>
// Paired intervals from the SAME frame: upscale, RCAS, and their total.
// Six sets avoid stalls; unfinished or disjoint results are never reported.
class GPUTimer {
public:
    struct Result { double first{}, second{}, total{}; };
    void Initialize(ID3D11Device* device) {
        device->GetImmediateContext(context_.put());
        for(auto& q:queries_) {
            D3D11_QUERY_DESC d{D3D11_QUERY_TIMESTAMP_DISJOINT,0}; winrt::check_hresult(device->CreateQuery(&d,q.disjoint.put()));
            d.Query=D3D11_QUERY_TIMESTAMP;
            for(auto& stamp:q.stamps) winrt::check_hresult(device->CreateQuery(&d,stamp.put()));
        }
    }
    void Reset() { ++generation_; result_.reset(); samples_=0; latestEnd_=0; }
    void Begin() {
        active_=nullptr;
        for(auto& q:queries_) if(q.pending) {
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT d{}; UINT64 t[3]{};
            auto ready=[&](ID3D11Query* query,void* data,UINT bytes) {
                auto hr=context_->GetData(query,data,bytes,D3D11_ASYNC_GETDATA_DONOTFLUSH);
                winrt::check_hresult(hr); return hr==S_OK;
            };
            if(!ready(q.disjoint.get(),&d,sizeof(d))) continue;
            bool done=true;
            for(int i=0;i<3;++i) done=ready(q.stamps[i].get(),&t[i],sizeof(UINT64)) && done;
            if(!done) continue;
            q.pending=false;
            if(q.generation==generation_ && !d.Disjoint && d.Frequency && t[2]>=t[1] && t[1]>=t[0]) {
                ++samples_;
                if(t[2]>=latestEnd_) {
                    const double ms=1000.0/double(d.Frequency);
                    result_=Result{(t[1]-t[0])*ms,(t[2]-t[1])*ms,(t[2]-t[0])*ms}; latestEnd_=t[2];
                }
            }
        }
        for(auto& q:queries_) if(!q.pending) { active_=&q; break; }
        if(active_) { context_->Begin(active_->disjoint.get()); context_->End(active_->stamps[0].get()); }
    }
    void Split() { if(active_) context_->End(active_->stamps[1].get()); }
    void End() {
        if(active_) {
            context_->End(active_->stamps[2].get()); context_->End(active_->disjoint.get());
            active_->pending=true; active_->generation=generation_; active_=nullptr;
        }
    }
    std::optional<Result> Value() const { return result_; }
    uint64_t Samples() const { return samples_; }
private:
    struct Query { winrt::com_ptr<ID3D11Query> disjoint; std::array<winrt::com_ptr<ID3D11Query>,3> stamps; bool pending{}; uint64_t generation{}; };
    winrt::com_ptr<ID3D11DeviceContext> context_;
    std::array<Query,6> queries_;
    Query* active_{};
    uint64_t generation_{},samples_{},latestEnd_{};
    std::optional<Result> result_;
};
