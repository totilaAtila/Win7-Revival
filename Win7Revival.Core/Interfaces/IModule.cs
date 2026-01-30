using System.ComponentModel;

namespace Win7Revival.Core.Interfaces
{
    /// <summary>
    /// Interfața comună pentru toate modulele de personalizare.
    /// Implementează INotifyPropertyChanged pentru data binding reactiv în UI.
    /// </summary>
    public interface IModule : INotifyPropertyChanged
    {
        /// <summary> Numele modulului (Ex: "Taskbar Transparent"). </summary>
        string Name { get; }
        
        /// <summary> Descriere user-friendly. </summary>
        string Description { get; }
        
        /// <summary> Statusul curent al modulului (activ/inactiv). </summary>
        bool IsEnabled { get; }

        /// <summary> Setup inițial, apelat o singură dată la startup. </summary>
        Task InitializeAsync();
        
        /// <summary> Pornește funcționalitatea modulului. Trebuie să fie idempotent. </summary>
        Task EnableAsync();
        
        /// <summary> Oprește funcționalitatea și curăță TOATE resursele. </summary>
        Task DisableAsync();
        
        /// <summary> Persistează starea și setările modulului. </summary>
        Task SaveSettingsAsync();
    }
}
